#include <stdio.h>
#include <vector>
#include <cstring> 
#include <cstdlib>
#include <cmath>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/sync.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"

extern "C" {
	#include "LCD_Driver.h"
	#include "LCD_Touch.h"
	#include "LCD_GUI.h"
	#include "DEV_Config.h"
}
#include "LCD_app.h"
#include "request.hpp"
#include "position_estimator.hpp"
#include "sd_card_wrapper.hpp"
#include "main.h"

#include "icm20948.h"


// Dedicated stack: the default core 1 stack is only 4 KB, which FatFs (long file name buffers) and
// float printf can exceed; an overflow corrupts the core 0 stack that sits right below it.
// It must be 8-byte aligned (AAPCS): a uint32_t array is only 4-byte aligned, and a misaligned stack
// breaks double arguments (printf "%f") and soft-float maths on this core.
static uint32_t core1_stack[4096] __attribute__((aligned(8)));   // 16 KB
static queue_t capture_queue;
static int16_t raw_samples[kMaxRecordingSamples][3];    // ax, ay, az in counts
static float positions[kMaxRecordingSamples][3];        // x, y, z in metres, gesture part only
static volatile uint32_t chars_finished = 0;   // characters fully handled (file closed) by core 1

static int32_t save_failure = 0;   // step * 100 + FatFs result of the first failure in the session


const uint32_t MAX_DATA_COLLECTION_TIME_US = 3 * 1000 * 1000; // 3 seconds

// Constructed without touching the hardware; mounted on core 0 in main() before core 1 starts.
static SD_CardWrapper sd_card(SD_CardWrapper::NoHalt{});

static HttpRequest* http_request = nullptr;   // created after Wi-Fi is up
static char boot_tag[9];                      // makes request ids unique across reboots (session ids restart at 1)
OCR_Inference inference;

static inline void debug_unpause_timer(void) {
    timer_hw->dbgpause = 0; 
}


static void note_failure(int step) {
    if (save_failure == 0) save_failure = step * 100 + (int)sd_card.lastResult();
    printf("SD step %d failed, FatFs result %d\n", step, (int)sd_card.lastResult());
}

// Opens /AW/<session>/c<index>_pos.csv for streaming. Returns false if the card is not usable.
static bool open_position_file(uint32_t session_id, uint8_t char_index) {
    char dir[32], path[48];
    snprintf(dir, sizeof dir, "AW/%lu", (unsigned long)session_id);
    snprintf(path, sizeof path, "%s/c%u_pos.csv", dir, char_index);

    if (!sd_card.isMounted()) { note_failure(STEP_MOUNT); return false; }
    if (!sd_card.makeDir("AW") || !sd_card.makeDir(dir)) { note_failure(STEP_MKDIR); return false; }
    if (!sd_card.openStream(path)) { note_failure(STEP_OPEN); return false; }

    static const char header[] = "x_m,y_m,z_m\n";
    if (!sd_card.writeStream(header, sizeof(header) - 1)) { note_failure(STEP_WRITE); return false; }
    return true;
}

// core1: computes the trajectory of every captured character (same method as the notebook) and writes it to
// the SD card, then reports to the UI.
void core1_entry() {
    CaptureMsg m;
    bool session_failed = false;

    while (true) {
        queue_remove_blocking(&capture_queue, &m);

        if (m.type == MSG_CHAR_CAPTURED) {
            bool storage_ok = open_position_file(m.session_id, m.char_index);
            bool estimate_ok = false;

            if (storage_ok) {
                // Apply position estimation to the raw IMU samples.
                estimate_ok = estimate_trajectory(raw_samples, m.sample_count, m.prefix_count, m.sample_rate_hz,
                                                  (float)AW_LPF_CUTOFF_HZ, positions);
                if (!estimate_ok) {
                    if (save_failure == 0) save_failure = STEP_ESTIMATE * 100;
                    printf("Trajectory estimation failed: %lu samples, %lu stationary, %.1f Hz\n",
                           (unsigned long)m.sample_count, (unsigned long)m.prefix_count, m.sample_rate_hz);
                }
            }

            if (storage_ok && estimate_ok) {
                // Downsample the trajectory to the output rate. and write it to the SD card.
                const uint32_t points = m.sample_count - m.prefix_count;
                uint32_t stride = (uint32_t)(m.sample_rate_hz / OUTPUT_RATE_HZ + 0.5f);
                if (stride < 1) stride = 1;

                float min_v[3] = {positions[0][0], positions[0][1], positions[0][2]};
                float max_v[3] = {min_v[0], min_v[1], min_v[2]};
                uint32_t written = 0;
                for (uint32_t k = 0; k < points && storage_ok; k++) {
                    for (int i = 0; i < 3; i++) {
                        if (positions[k][i] < min_v[i]) min_v[i] = positions[k][i];
                        if (positions[k][i] > max_v[i]) max_v[i] = positions[k][i];
                    }
                    if (k % stride != 0 && k != points - 1) continue;

                    // Metres in the sensor frame, like the notebook's "position" (matches the "x_m,y_m,z_m"
                    // header): build_trajectory() reads these back with strtod() when the character is
                    // uploaded, so keep this in sync with that format.
                    char line[80];
                    const int n = snprintf(line, sizeof line, "%.4f,%.4f,%.4f\n",
                                           positions[k][0], positions[k][1], positions[k][2]);
                    storage_ok = sd_card.writeStream(line, n);
                    if (!storage_ok) note_failure(STEP_WRITE);
                    written++;
                }
                printf("Character %u: %lu samples at %.0f Hz, %lu points written, extent x=%.3f y=%.3f z=%.3f m\n",
                       m.char_index + 1, (unsigned long)m.sample_count, m.sample_rate_hz, (unsigned long)written,
                       max_v[0] - min_v[0], max_v[1] - min_v[1], max_v[2] - min_v[2]);
            }
            if (sd_card.isStreamOpen() && !sd_card.closeStream()) {
                note_failure(STEP_CLOSE);
                storage_ok = false;
            }
            chars_finished = chars_finished + 1;

            if (storage_ok && estimate_ok) {
                AppEvent e = {};
                e.type = EVT_CHARACTER_SAVED;
                e.char_index = m.char_index;
                e.char_count = m.char_count;
                e.session_id = m.session_id;
                printf("Character %u/%u saved for session %lu\n", m.char_index + 1, m.char_count, (unsigned long)m.session_id);
                app_post_critical(&e);
            } else {
                // Reported once, when the whole session is finished and the SPI bus is free again.
                session_failed = true;
                printf("Character %u/%u could not be saved\n", m.char_index + 1, m.char_count);
            }
        } else if (m.type == MSG_SESSION_END) {
            printf("Session %lu finished\n", (unsigned long)m.session_id);

            // Touch scanning was paused during the session (the SD card shares the SPI bus); resume it
            // before the UI is told to redraw.
            inference.state = DEFAULT;
            AppEvent done = {};
            done.char_index = NO_CHAR;
            done.char_count = m.char_count;
            done.session_id = m.session_id;
            if (session_failed) {
                done.type = EVT_ERROR;
                done.stage = STAGE_SAVE;
                done.error = AERR_STORAGE_WRITE;
                done.detail = save_failure;
            } else {
                done.type = EVT_ALL_SAVED;
            }
            app_post_critical(&done);
            session_failed = false;
            save_failure = 0;
        }
    }
}

// Polls the accelerometer back to back for duration_us and appends the samples to raw_samples, like the loop of
// lab_0_1.cpp. Returns the elapsed time in microseconds.
static uint32_t record_samples(uint32_t duration_us, uint32_t& count) {
    IMU_ST_SENSOR_DATA gyro, accel;
    const uint32_t start_us = time_us_32();
    uint32_t elapsed_us = 0;
    while (count < kMaxRecordingSamples && (elapsed_us = time_us_32() - start_us) < duration_us) {
        imuDataAccGyrGet(&gyro, &accel);
        raw_samples[count][0] = accel.s16X;
        raw_samples[count][1] = accel.s16Y;
        raw_samples[count][2] = accel.s16Z;
        count++;
    }
    return time_us_32() - start_us;
}

// core0: records the characters
static void capture_characters(const AppCommand& cmd) {
    // prevent_interaction() makes TP_DrawBoard() skip the touch scan, so core 0 does not use the
    // SPI bus while core 1 writes to the SD card. Reset by core 1 when the session is finished.
    inference.state = PROCESSING;
    chars_finished = 0;

    for (uint8_t index = 0; index < cmd.char_count; index++) {
        // Core 1 must have finished the previous character before the buffers are reused (it always
        // counts the character, also when saving fails).
        while (chars_finished < index) sleep_ms(5);

        // Hold-still countdown: the first samples of every character are the stationary prefix.
        APP_ShowCapturePrompt(index, cmd.char_count, "");
        for (int sec = PREPARE_SECONDS; sec > 0; sec--) {
            char line[24];
            snprintf(line, sizeof line, "Hold still... %d", sec);
            APP_ShowCapturePrompt(index, cmd.char_count, line);
            sleep_ms(1000);
        }
        APP_ShowCapturePrompt(index, cmd.char_count, "Hold still...");

        uint32_t count = 0;
        record_samples(STATIONARY_US, count);
        const uint32_t prefix_count = count;

        // The display update takes a while; the writer is still reacting to the prompt, so the gap between
        // the prefix and the gesture is still part of the stationary phase.
        APP_ShowCapturePrompt(index, cmd.char_count, "Write now!");
        const uint32_t gesture_us = record_samples(MAX_DATA_COLLECTION_TIME_US, count);

        CaptureMsg msg = {};
        msg.type = MSG_CHAR_CAPTURED;
        msg.char_index = index;
        msg.char_count = cmd.char_count;
        msg.session_id = cmd.session_id;
        msg.sample_count = count;
        msg.prefix_count = prefix_count;
        msg.sample_rate_hz = gesture_us ? (float)(count - prefix_count) * 1e6f / (float)gesture_us : 0.0f;
        queue_add_blocking(&capture_queue, &msg);
    }

    CaptureMsg done = {};
    done.type = MSG_SESSION_END;
    done.char_count = cmd.char_count;
    done.session_id = cmd.session_id;
    queue_add_blocking(&capture_queue, &done);
}

static void post_upload_error(const AppCommand& cmd, uint8_t stage, uint8_t error, int32_t detail, const char* text = "") {
    AppEvent e = {};
    snprintf(e.text, sizeof e.text, "%s", text);
    e.type = EVT_ERROR;
    e.session_id = cmd.session_id;
    e.char_count = cmd.char_count;
    e.char_index = NO_CHAR;
    e.stage = stage;
    e.error = error;
    e.detail = detail;
    app_post_critical(&e);
}

// Reads AW/<session>/c<index>_pos.csv (header line, then "x_m,y_m,z_m" per line, already in metres -
// see open_position_file()) and builds the x string. Returns the number of points, or -1 if the file
// cannot be read.
static int build_trajectory(uint32_t session_id, uint8_t char_index, std::string& x) {
    char path[48];
    snprintf(path, sizeof path, "AW/%lu/c%u_pos.csv", (unsigned long)session_id, char_index);

    std::vector<uint8_t> data;
    if (!sd_card.readFile(path, data)) return -1;
    data.push_back('\0');   // strtod needs a terminator

    const char* p = reinterpret_cast<const char*>(data.data());
    const char* end = p + data.size() - 1;
    while (p < end && *p != '\n') p++;      // skip the header
    if (p < end) p++;

    x.clear();
    x.reserve(data.size() * 2);
    int points = 0;
    while (p < end) {
        double v[3];
        int k = 0;
        for (; k < 3; k++) {
            char* q;
            v[k] = strtod(p, &q);
            if (q == p) break;
            p = q;
            if (*p == ',') p++;
        }
        if (k == 3) {
            if (points) x.append(",");
            char num[24];
            snprintf(num, sizeof num, "%.4f", v[0]); x.append(num); x.append(",");
            snprintf(num, sizeof num, "%.4f", v[1]); x.append(num); x.append(",");
            snprintf(num, sizeof num, "%.4f", v[2]); x.append(num);
            points++;
        }
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    return points;
}

static void send_ocr_request(const AppCommand& cmd) {
    printf("Sending word: char_count=%d, session_id=%lu\n", cmd.char_count, (unsigned long)cmd.session_id);

    // One id per word: the server keeps every id's images and its final answer, so reusing an id gives a stale
    // result or "total_packets_mismatch". boot_tag keeps ids unique across reboots (session ids restart at 1).
    char request_id[40];
    snprintf(request_id, sizeof request_id, "%s_%s_%lu", REQUEST_ID, boot_tag, (unsigned long)cmd.session_id);

    std::string x, body;
    for (uint8_t i = 0; i < cmd.char_count; i++) {
        const int points = build_trajectory(cmd.session_id, i, x);
        if (points < 0) {
            printf("[Upload] Cannot read character %u from the SD card\n", i);
            post_upload_error(cmd, STAGE_UPLOAD, AERR_STORAGE_READ, i);
            return;
        }

        char head[96];
        snprintf(head, sizeof head, "{\"request_id\": \"%s\", \"packet_index\": %u, \"total_packets\": %u, \"x\": \"",
                 request_id, i, cmd.char_count);
        body.assign(head);
        body.append(x);
        body.append("\"}");
        printf("[Upload] Character %u/%u: %d points, %u bytes\n", i + 1, cmd.char_count, points, (unsigned)body.size());

        ResponseDataStr response = {};
        bool ok = false;
        char server_error[sizeof response.error] = "";
        for (int attempt = 0; attempt < 2 && !ok; attempt++) {
            http_request->set_body_raw(body.c_str(), body.size());
            response = http_request->post();
            ok = response.status_ok;
            if (ok) break;

            printf("[Upload] Packet %u failed (attempt %d): HTTP %d %s\n", i, attempt + 1, response.http_status,
                   response.error[0] ? response.error : "no valid response");
            snprintf(server_error, sizeof server_error, "%s", response.error);
            const bool rejected = response.http_status >= 400 && response.http_status < 500;   // retrying cannot help
            if (response.y) { free(response.y); response.y = nullptr; }
            http_request->reinitialize();
            if (rejected) break;
            sleep_ms(300);
        }
        if (!ok) {
            post_upload_error(cmd, STAGE_UPLOAD, AERR_SERVER_REJECTED, i, server_error[0] ? server_error : "no response");
            return;
        }

        printf("[Upload] Packet %u/%u response: %s\n", i + 1, cmd.char_count, response.y ? response.y : "none");
        if (i == cmd.char_count - 1) {
            if (!response.y || !response.y[0]) {
                post_upload_error(cmd, STAGE_OCR, AERR_BAD_RESPONSE, i);
            } else {
                AppEvent result = {};
                result.type = EVT_OCR_RESULT;
                result.session_id = cmd.session_id;
                result.char_count = cmd.char_count;
                result.char_index = NO_CHAR;
                snprintf(result.text, sizeof result.text, "%s", response.y);
                app_post_critical(&result);
            }
        }
        if (response.y) free(response.y);
        http_request->reinitialize();
        sleep_ms(300);
    }
}

int main(void)
{
    System_Init();

    debug_unpause_timer();  

    sleep_ms(5000);

    if (cyw43_arch_init()) {
        printf("Wi-Fi chip init failed\n");
        return -1;
    }
    printf("Wi-Fi chip initialized successfully\n");

    cyw43_arch_enable_sta_mode();

    // Connect to the WiFI network - loop until connected
    printf("Start connecting wifi.\n");
    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000) != 0){
        printf("Attempting to connect...\n");
    }
    // Print a success message once connected
    printf("WiFi connected successfully! \n");
    
	LCD_SCAN_DIR  lcd_scan_dir = SCAN_DIR_DFT;
	APP_Init(lcd_scan_dir, &inference);

    IMU_EN_SENSOR_TYPE enMotionSensorType;
    imuInit(&enMotionSensorType);
    if (enMotionSensorType == IMU_EN_SENSOR_TYPE_NULL) {
        printf("IMU not found\n");
        return -1;
    }

    FRESULT sd_result;
    if (!sd_card.mount(&sd_result)) {
        printf("SD card mount failed (%d), recordings will not be saved\n", sd_result);
    }

    queue_init(&capture_queue, sizeof(CaptureMsg), CAPTURE_QUEUE_DEPTH);

    // run core1 loop that computes and stores the trajectories
    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));

    HttpRequest request(URL_REQUEST, HOST, PORT);
    http_request = &request;
    snprintf(boot_tag, sizeof boot_tag, "%08lx", (unsigned long)get_rand_32());

    LCD_SetBackLight(1000);

    // core0: user interface, command dispatch and IMU sampling
    while (1) {
        TP_DrawBoard();
        APP_Poll();

        AppCommand cmd;
        if (!queue_try_remove(&command_queue, &cmd)) continue;

        if (cmd.type == CMD_START_CAPTURE) {
            // Start capturing characters from the IMU sensor
            capture_characters(cmd);
        } else if (cmd.type == CMD_SEND_WORD) {
            // Send the captured word to the OCR server character by character
            send_ocr_request(cmd);
        }
    }

    return 0;
}

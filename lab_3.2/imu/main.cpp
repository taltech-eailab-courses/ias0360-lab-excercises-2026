#include <stdio.h>
#include <vector>
#include <cstring> 
#include <sstream>

#include "pico/sync.h"
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/timer.h"

#include "request.hpp"
#include "sd_card_wrapper.hpp"

#define NUMBER_OF_CSV_FILES 3
#define REQUEST_ID "YOUR_STUDENT_ID"

static HttpRequest* http_request = nullptr;   // created after Wi-Fi is up
static SD_CardWrapper sd_card(SD_CardWrapper::NoHalt{});

static inline void debug_unpause_timer(void) {
    timer_hw->dbgpause = 0; 
}

int init_wifi_connection(void) {

	stdio_init_all();
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
    while(cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000) != 0) {
        printf("Attempting to connect...\n");
    }
    // Print a success message once connected
    printf("WiFi connected successfully! \n");

    sleep_ms(3000);

    return 0;
}

static int build_trajectory(uint8_t char_index, std::string& x) {
    char path[48];
    snprintf(path, sizeof path, "position_%u.csv", char_index);

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


int post_request_with_chunked_body_from_sd_card(void) {
    /*
    This function reads trajectory data from multiple CSV files on the SD card,
    constructs JSON packets, and sends them to the server using HTTP POST requests.
    The request body contains JSON objects with the following structure:
    {
        "request_id": "<unique_request_id>",
        "packet_index": <current_packet_index>,
        "total_packets": <total_number_of_packets>,
        "x": "<trajectory_data>"
    }.
    Once all packets have been sent, the server combines them to construct an image,
    on which the OCR is performed. The recognized text is returned to the client.
    */

    printf("Sending word: char_count=%d\n", NUMBER_OF_CSV_FILES);

    char boot_tag[9];
    snprintf(boot_tag, sizeof boot_tag, "%08lx", (unsigned long)get_rand_32());
    char request_id[40];
    snprintf(request_id, sizeof request_id, "%s_%s", REQUEST_ID, boot_tag);

    std::string x, body;
    for (uint8_t i = 0; i < NUMBER_OF_CSV_FILES; i++) {
        const int points = build_trajectory(i, x);
        if (points < 0) {
            printf("[Upload] Cannot read character %u from the SD card\n", i);
            return -1;
        }

        char head[96];
        snprintf(head, sizeof head, "{\"request_id\": \"%s\", \"packet_index\": %u, \"total_packets\": %u, \"x\": \"",
                 request_id, i, NUMBER_OF_CSV_FILES);
        body.assign(head);
        body.append(x);
        body.append("\"}");
        printf("[Upload] Character %u/%u: %d points, %u bytes\n", i + 1, NUMBER_OF_CSV_FILES, points, (unsigned)body.size());

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
            printf("[Upload] Packet %u failed permanently: %s\n", i, server_error[0] ? server_error : "no response");
            return -1;
        }
    
        printf("[Upload] Packet %u/%u response: %s\n", i + 1, NUMBER_OF_CSV_FILES, response.y ? response.y : "none");
        if (i == NUMBER_OF_CSV_FILES - 1) {
            if (!response.y || !response.y[0]) {
                printf("[Upload] Packet %u/%u failed: bad response\n", i + 1, NUMBER_OF_CSV_FILES);
            } else {
                printf("[Upload] Packet %u/%u succeeded: %s\n", i + 1, NUMBER_OF_CSV_FILES, response.y ? response.y : "none");
            }
        }

        if (response.y) free(response.y);
        http_request->reinitialize();
        sleep_ms(300);
    }

    printf("[Upload] All packets uploaded successfully\n");
    return 0;
}

// TODO: 1. copy csv files located at tmp/ to SD card before posting request. 
//       2. implement the function post_request_with_chunked_body_from_sd_card to read the CSV files from the SD card and send them as a chunked HTTP request.

int main(void)
{
    if (init_wifi_connection() != 0) {
        printf("Wi-Fi connection failed\n");
        return -1;
    }

    FRESULT sd_result;
    if (!sd_card.mount(&sd_result)) {
        printf("SD card mount failed (%d)\n", sd_result);
        return -1;
    }

    HttpRequest request(URL_REQUEST, HOST, PORT);
    http_request = &request;

    if (post_request_with_chunked_body_from_sd_card() != 0) {
        printf("Failed to post request with chunked body from SD card\n");
        return -1;
    }

    return 0;
}
#include <stdio.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "hardware/pio.h"

#include "model.h"
#include "model_settings.h"
#include "gesture_model_data.h"
#include "data_preprocessor.hpp"

// Included last: this header defines `true`/`false` as macros.
#include "icm20948.h"
#include "ws2812.pio.h"

#define HALT() while (1) { tight_loop_contents(); }

static const char* const class_labels[CLASS_COUNT] = CLASS_LABELS;

static_assert(MAX_GESTURE_RAW_DATA_LEN == kInputWindowLength, "Raw gesture length must match the preprocessing window");
static_assert(GESTURE_DATA_MAX_LEN == kTargetLength, "Model input length must match the preprocessing target");

const uint WS2812_PIN = 4;
// Data collection duration of one gesture.
const uint32_t MAX_DATA_COLLECTION_TIME_US = 2 * 1000 * 1000; // 2 seconds

typedef struct __attribute__((packed))
{
    int16_t ax, ay, az;
    uint8_t end_of_gesture;
    uint8_t _pad;                // pad to 8 bytes total
} imu_sample_t;

// Core 1 drains the queue while core 0 samples, so it only needs to absorb bursts.
#define QUEUE_DEPTH (2048)
static queue_t sample_q;

// Core 1 -> core 0 messages go through the inter-core FIFO.
constexpr uint32_t MSG_CORE1_READY = 0xA0000000u;  // model + preprocessor initialized
constexpr uint32_t MSG_CORE1_FAILED = 0xAFFFFFFFu; // initialization failed
constexpr uint32_t MSG_RESULT_BASE = 0xB0000000u;  // MSG_RESULT_BASE + class index
constexpr uint32_t MSG_RESULT_ERROR = 0xBFFFFFFFu; // inference failed

// Number of samples core 0 could not enqueue for the current gesture.
static volatile uint32_t dropped_samples = 0;

// TFLite Micro needs more stack than the default core 1 stack.
static uint32_t core1_stack[4096] __attribute__((aligned(8)));

// Raw samples interleaved as (ax, ay, az) per time step, as DataPreprocessor expects.
static int16_t raw_interleaved[MAX_GESTURE_RAW_DATA_LEN * GESTURE_DATA_IMU_AXIS];

Model ml_model;
DataPreprocessor preprocessor;

static inline void show_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    pio_sm_put_blocking(pio0, 0, (((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b) << 8u);
}

static void init_pio_for_ws2812()
{
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, true);
}

static void core1_fail(const char* msg)
{
    printf("%s\n", msg);
    multicore_fifo_push_blocking(MSG_CORE1_FAILED);
    HALT();
}

// Core 1: initialize the model, then preprocess + classify every gesture received from core 0.
static void core1_entry(void)
{
    if (!ml_model.setup()) {
        core1_fail("Failed to initialize ML model!");
    }
    printf("Model initialized\n");

    uint8_t* input_tensor = ml_model.input_data();
    if (input_tensor == nullptr) {
        core1_fail("Cannot set input");
    }

    int byte_size = ml_model.byte_size();
    if (!byte_size) {
        core1_fail("Byte size not found");
    }

    const TfLiteTensor* output = ml_model.output_tensor();
    if (output == nullptr) {
        core1_fail("Cannot get output");
    }
    const int output_count = output->dims->data[output->dims->size - 1];
    if (output->type != kTfLiteUInt8 || output_count != CLASS_COUNT) {
        printf("Unexpected output tensor: type=%d, classes=%d\n", output->type, output_count);
        core1_fail("Output tensor mismatch");
    }

    // The model input is (time, axis) interleaved: [t0:ax,ay,az, t1:ax,ay,az, ...].
    constexpr int kInputBytes = GESTURE_DATA_MAX_LEN * GESTURE_DATA_IMU_AXIS;
    if (byte_size != kInputBytes) {
        printf("Input size mismatch: model expects %d bytes, gesture has %d\n", byte_size, kInputBytes);
        core1_fail("Input tensor mismatch");
    }

    multicore_fifo_push_blocking(MSG_CORE1_READY);

    for (;;) {
        // Collect one gesture. Samples beyond the preprocessing window are
        // drained but discarded, i.e. the trailing samples are cut.
        size_t sample_count = 0;
        size_t received = 0;
        imu_sample_t s;
        do {
            queue_remove_blocking(&sample_q, &s);
            if (s.end_of_gesture) {
                break;
            }
            received++;
            if (sample_count < MAX_GESTURE_RAW_DATA_LEN) {
                raw_interleaved[sample_count * GESTURE_DATA_IMU_AXIS + 0] = s.ax;
                raw_interleaved[sample_count * GESTURE_DATA_IMU_AXIS + 1] = s.ay;
                raw_interleaved[sample_count * GESTURE_DATA_IMU_AXIS + 2] = s.az;
                sample_count++;
            }
        } while (true);

        printf("Received %u samples (%u dropped), using %u\n",
               (unsigned)received, (unsigned)dropped_samples, (unsigned)sample_count);

        // normalize, pad/cut, downsample and quantize straight into the model's input tensor
        if (!preprocessor.preprocess(raw_interleaved, sample_count, input_tensor,
                                     ml_model.input->params.scale,
                                     ml_model.input->params.zero_point)) {
            printf("Preprocessing failed\n");
            multicore_fifo_push_blocking(MSG_RESULT_ERROR);
            continue;
        }

        absolute_time_t inference_start = get_absolute_time();
        int result = ml_model.predict();
        float inference_time_ms = absolute_time_diff_us(inference_start, get_absolute_time()) / 1000.0f;

        if (result < 0 || result >= CLASS_COUNT) {
            printf("Failed to run inference\n");
            multicore_fifo_push_blocking(MSG_RESULT_ERROR);
            continue;
        }

        for (int c = 0; c < CLASS_COUNT; c++) {
            // real_value = scale * (quantized_value - zero_point)
            float probability = output->data.uint8[c] / 255.0f;
            printf("  %-20s raw=%3u prob=%.3f\n", class_labels[c], output->data.uint8[c], probability);
        }
        printf("Predicted: %s (class %d)\n", class_labels[result], result);
        printf("Inference time: %.2f ms\n\n", inference_time_ms);

        multicore_fifo_push_blocking(MSG_RESULT_BASE + (uint32_t)result);
    }
}

// Core 0: sample the IMU for MAX_DATA_COLLECTION_TIME_US and push every sample to core 1.
int main(void)
{
    stdio_init_all();

    sleep_ms(3000);
    printf("Starting the gesture recognition test...\n");

    init_pio_for_ws2812();
    show_color_rgb(255, 0, 0); // red: initializing

    queue_init(&sample_q, sizeof(imu_sample_t), QUEUE_DEPTH);
    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));

    IMU_EN_SENSOR_TYPE enMotionSensorType;
    IMU_ST_SENSOR_DATA stGyroRawData, stAccelRawData;
    imuInit(&enMotionSensorType);
    if (enMotionSensorType == IMU_EN_SENSOR_TYPE_NULL) {
        printf("IMU not found\n");
        HALT();
    }
    printf("Device setup complete on core0\n");

    if (multicore_fifo_pop_blocking() != MSG_CORE1_READY) {
        printf("Core 1 failed to initialize\n");
        HALT();
    }
    printf("Device setup complete on core1\n");

    for (;;) {
        // Give the user time to get ready before sampling starts.
        show_color_rgb(0, 0, 255);
        sleep_ms(2000);

        printf("Perform a gesture now (%u ms)...\n", (unsigned)(MAX_DATA_COLLECTION_TIME_US / 1000));
        show_color_rgb(0, 255, 0); // green: recording

        dropped_samples = 0;
        uint64_t start_time = time_us_64();
        while (time_us_64() < start_time + MAX_DATA_COLLECTION_TIME_US) {
            imuDataAccGyrGet(&stGyroRawData, &stAccelRawData);

            imu_sample_t s = {};
            s.ax = stAccelRawData.s16X;
            s.ay = stAccelRawData.s16Y;
            s.az = stAccelRawData.s16Z;

            if (!queue_try_add(&sample_q, &s)) {
                dropped_samples = dropped_samples + 1;
            }
        }

        // Tell core 1 that the gesture is complete.
        imu_sample_t end = {};
        end.end_of_gesture = 1;
        queue_add_blocking(&sample_q, &end);

        show_color_rgb(255, 255, 0); // yellow: core 1 is processing

        uint32_t msg = multicore_fifo_pop_blocking();
        if (msg >= MSG_RESULT_BASE && msg < MSG_RESULT_BASE + CLASS_COUNT) {
            printf("Gesture: %s\n\n", class_labels[msg - MSG_RESULT_BASE]);
            show_color_rgb(0, 255, 255); // cyan: result available
        } else {
            printf("Gesture recognition failed\n\n");
            show_color_rgb(255, 0, 0);
        }
        sleep_ms(2000);
    }
    return 0;
}

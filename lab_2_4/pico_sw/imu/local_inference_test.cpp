#include <cmath> 
#include<iostream>
#include <cstdlib> 
#include <iostream>
#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/watchdog.h"
#include "hardware/sync.h"

#include "model.h"
#include "model_settings.h"
#include "gesture_model_data.h"
#include "gesture_data_preprocessed.h"

using namespace std; 
#define HALT_CORE_1() while (1) { tight_loop_contents(); }

static const char* const class_labels[CLASS_COUNT] = CLASS_LABELS;

// Preprocessed (normalized + uint8-quantized) gestures, stored axis-major as [axis][time].
// Index i corresponds to class i in class_labels.
static const uint8_t (*const test_gestures[CLASS_COUNT])[GESTURE_DATA_MAX_LEN] = {
    gesture_data_preprocessed_0,
    gesture_data_preprocessed_1,
    gesture_data_preprocessed_2,
    gesture_data_preprocessed_3,
};

Model ml_model;

int main(void) 
{
    stdio_init_all(); 

    sleep_ms(3000);
    printf("Starting the gesture recognition test...\n");

        // initialize ML model
    if (!ml_model.setup()) {
        printf("Failed to initialize ML model!\n");
        HALT_CORE_1();
    }
    printf("Model initialized\n");
    
    uint8_t* input_tensor = ml_model.input_data();
    if (input_tensor == nullptr) {
        printf("Cannot set input\n");
        HALT_CORE_1();
    }
    
    int byte_size = ml_model.byte_size();
    if (!byte_size) {
        printf("Byte size not found\n");
        HALT_CORE_1();
    }

    const TfLiteTensor* output = ml_model.output_tensor();
    if (output == nullptr) {
        printf("Cannot get output\n");
        HALT_CORE_1();
    }
    const int output_count = output->dims->data[output->dims->size - 1];
    if (output->type != kTfLiteUInt8 || output_count != CLASS_COUNT) {
        printf("Unexpected output tensor: type=%d, classes=%d\n", output->type, output_count);
        HALT_CORE_1();
    }

    // The model input is (time, axis) interleaved: [t0:ax,ay,az, t1:ax,ay,az, ...],
    // while the stored gesture is [axis][time], so it must be transposed.
    constexpr int kInputBytes = GESTURE_DATA_MAX_LEN * GESTURE_DATA_IMU_AXIS;
    if (byte_size != kInputBytes) {
        printf("Input size mismatch: model expects %d bytes, gesture has %d\n", byte_size, kInputBytes);
        HALT_CORE_1();
    }

    while (true) {
        for (int i = 0; i < CLASS_COUNT; i++) {

            const uint8_t (*gesture)[GESTURE_DATA_MAX_LEN] = test_gestures[i];
            for (int t = 0; t < GESTURE_DATA_MAX_LEN; t++) {
                for (int axis = 0; axis < GESTURE_DATA_IMU_AXIS; axis++) {
                    input_tensor[t * GESTURE_DATA_IMU_AXIS + axis] = gesture[axis][t];
                }
            }

            absolute_time_t inference_start = get_absolute_time();
            int result = ml_model.predict();
            float inference_time_ms = absolute_time_diff_us(inference_start, get_absolute_time()) / 1000.0f;
        
            if (result < 0 || result >= CLASS_COUNT) {
                printf("Failed to run inference\n");
            } else {
                printf("Expected: %s\n", class_labels[i]);
                for (int i = 0; i < CLASS_COUNT; i++) {
                    // real_value = scale * (quantized_value - zero_point)
                    float probability = output->params.scale * (output->data.uint8[i] - output->params.zero_point);
                    printf("  %-20s raw=%3u prob=%.3f\n", class_labels[i], output->data.uint8[i], probability);
                }
                printf("Predicted: %s (class %d)\n", class_labels[result], result);
                printf("Inference time: %.2f ms\n\n", inference_time_ms);
            }

            sleep_ms(2000);
        }
        sleep_ms(5000);
    }
    return 0;
}

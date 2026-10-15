#include <cmath>
#include <cstdlib>
#include <iostream>

#include "pico/stdlib.h"

#include "model.h"
#include "inference.h"
#include "model_settings.h"
#include "mnist_model_data.h"
#include "mnist_image_data.h"

using namespace std;

#define HALT_CORE_1() while (1) { tight_loop_contents(); }

// 10 hand-picked real MNIST test images (raw 0-255 pixel values, one per
// digit 0-9) baked in as C arrays -- see models/mnist_image_data.cpp.
// These let you sanity-check the on-device model without needing the LCD
// touchscreen or your own drawn digits wired up yet.
const uint8_t* test_dataset[] = {
    mnist_image_data_0,
    mnist_image_data_1,
    mnist_image_data_2,
    mnist_image_data_3,
    mnist_image_data_4,
    mnist_image_data_5,
    mnist_image_data_6,
    mnist_image_data_7,
    mnist_image_data_8,
    mnist_image_data_9
};

void inference_test(void)
{
    Model ml_model;

    if (!ml_model.setup()) {
        printf("Failed to initialize ML model!\n");
        HALT_CORE_1();
    }
    printf("Model initialized\n");

    int correct = 0;
    int total = 0;

    while (true) {
        int digit = rand() % 10;
        const uint8_t* sample_data = test_dataset[digit];

        // Preview the raw image in the serial console.
        for (int i = 0; i < image_row_size; i++) {
            for (int j = 0; j < image_col_size; j++) {
                printf("%3d", sample_data[image_col_size * i + j]);
            }
            printf("\n");
        }

        if (!ml_model.set_input_image(sample_data)) {
            printf("Failed to set input image\n");
            sleep_ms(2000);
            continue;
        }

        int result = ml_model.predict();
        total++;
        if (result == -1) {
            printf("Failed to run inference\n");
        } else {
            bool ok = (result == digit);
            correct += ok ? 1 : 0;
            printf("Actual: %d, Predicted: %d  [%s]\n", digit, result, ok ? "OK" : "WRONG");
            printf("Running accuracy: %d/%d\n", correct, total);
        }

        sleep_ms(3000);
    }
}

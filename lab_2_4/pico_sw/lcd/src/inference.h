#ifndef TFLITE_INFERENCE_TEST_INFERENCE_H_
#define TFLITE_INFERENCE_TEST_INFERENCE_H_

#include <stdio.h>
#include <cstdint>
#include "pico/stdlib.h"
#include "pico/multicore.h"
// #include "hardware/irq.h"


#define DIGIT_SIZE 28
#define NUM_BOX 4

void inference_test(void);

#endif // TFLITE_INFERENCE_TEST_INFERENCE_H_
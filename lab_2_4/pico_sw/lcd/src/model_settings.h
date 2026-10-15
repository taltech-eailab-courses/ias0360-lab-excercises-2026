#ifndef TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_
#define TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_

// NOTE: INPUT_IMAGE_SIZE (28), UNKNOWN_PREDICTION (100), and DIGIT_INPUT_COUNT
// (4, in LCD_Touch.h) are already #defined by lib/config/DEV_Config.h and
// lib/lcd/LCD_Touch.h -- do NOT redefine them here, or you'll get "macro
// redefined" warnings/errors depending on include order. This file only adds
// the constants those files don't already provide.

// Input image is 28x28 grayscale, matching MNIST and INPUT_IMAGE_SIZE above.
constexpr int image_row_size = 28;
constexpr int image_col_size = 28;
constexpr int kNumChannels = 1;
constexpr int kMaxImageSize = image_row_size * image_col_size * kNumChannels;

// MNIST has 10 classes: digits 0-9.
constexpr int kCategoryCount = 10;
extern const char* kCategoryLabels[kCategoryCount];

// TODO 3: Set the tensor arena size for your pruned + quantized model.
//
// Build and run `main_arena_size_test` FIRST (it prints
// interpreter->arena_used_bytes() every second over serial), then set this
// constant to that value plus a small safety margin (e.g. +1-2 KB). Starting
// with something clearly too big (e.g. 60 * 1024) so the first build/flash
// succeeds is a reasonable way to get your first arena-size reading; don't
// ship that number, though -- come back and tighten it once you know the
// real requirement.
constexpr int arena_size = 60 * 1024;

#endif  // TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_

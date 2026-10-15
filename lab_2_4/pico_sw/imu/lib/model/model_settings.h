#ifndef TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_
#define TFLITE_INFERENCE_TEST_MODEL_SETTINGS_H_

#define GESTURE_DATA_IMU_AXIS 3 // Typically, IMU has 3 axes: x, y, z.
#define GESTURE_DATA_MAX_LEN 128 // Maximum length of gesture data sequence.
#define MAX_GESTURE_RAW_DATA_LEN 4000

#define CLASS_COUNT 4
#define CLASS_LABELS {"Negative (Static)", "Ring (Circle)", "Slope (L-shape)", "Wing (W-shape)"}

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

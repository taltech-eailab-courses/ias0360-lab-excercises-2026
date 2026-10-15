#ifndef PREPROCESSING_SETTINGS_HPP
#define PREPROCESSING_SETTINGS_HPP

#include <cstdint>

// All sample buffers are (ax, ay, az) interleaved per time step, i.e.
// sample t, axis a is at index t * kNumAxes + a. This matches the TFLite
// model's input tensor layout (1, 128, 3, 1).
//
// DataPreprocessor stages: normalize -> pad/cut to kInputWindowLength
// -> downsample to kTargetLength -> quantize to uint8. It does no bias
// removal or low-pass filtering (see preprocessor.py's lowpass_filter).
constexpr int kNumAxes = 3;

// Number of samples per axis the model's input is built from (2 s at
// 2 kHz, matches MAX_GESTURE_RAW_DATA_LEN). Shorter recordings are
// padded, longer ones cut.
constexpr int kInputWindowLength = 4000;

// Number of samples per axis fed to the model (downsampled window).
constexpr int kTargetLength = 128;

// When padding, each axis is filled with the mode of its last
// kPaddingModeWindow samples (the stationary end of the gesture).
constexpr int kPaddingModeWindow = 100;

// Raw sensor counts -> g: g = raw_count / kCountsPerG (zero bias assumed).
// Must match preprocessor.COUNTS_PER_G.
constexpr float kCountsPerG = 16384.0f;

// Per-axis (ax, ay, az) normalization statistics fitted on the training
// split and saved in imu_dataset.npz ("mean"/"std"). Applied as
// normalized = (g - mean) / std. Re-extract these if the dataset/model
// is retrained.
constexpr float kAxisMean[kNumAxes] = {
    -0.01672462932765484f, -0.020025961101055145f, 0.9908939599990845f};
constexpr float kAxisStd[kNumAxes] = {
    0.19918961822986603f, 0.22678060829639435f, 0.3880577087402344f};

// Quantization parameters of gesture_model.tflite's input tensor:
// uint8 = round(normalized_value / scale) + zero_point, clipped to
// [0, 255]. Read via interpreter.get_input_details()[0]["quantization"]
// (see create_imu_test_set.py's get_input_quantization). Re-extract
// these any time the model is re-exported/requantized.
constexpr float kInputScale = 0.030684299767017365f;
constexpr int32_t kInputZeroPoint = 133;

#endif // PREPROCESSING_SETTINGS_HPP

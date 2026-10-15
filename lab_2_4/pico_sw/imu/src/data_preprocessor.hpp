#ifndef DATA_PREPROCESSOR_HPP
#define DATA_PREPROCESSOR_HPP

#include <cstddef>
#include <cstdint>

#include "preprocessing_settings.hpp"

// Turns a raw IMU recording into the uint8 input of the quantized model.
//
// Buffers are (ax, ay, az) interleaved: sample t, axis a is at t * kNumAxes + a.
// `length` always counts samples per axis (time steps), not values.
//
// The individual stages can be called separately; preprocess() runs them as:
//   1. Normalize        raw counts -> g -> (g - mean) / std       (float)
//   2. PadWithTailMode  length < kInputWindowLength -> pad with per-axis mode
//                       of the last kPaddingModeWindow samples
//   3. CutTrailing      length > kInputWindowLength -> drop the last samples
//   4. Downsample       kInputWindowLength -> kTargetLength (block average)
//   5. Quantize         float -> uint8 using the input tensor's scale/zero point
// preprocess() applies the cut (3) before normalizing, so only retained
// samples are normalized; the result is identical.
class DataPreprocessor {
public:
    DataPreprocessor() = default;

    // Full pipeline. Writes kTargetLength * kNumAxes bytes to `output`,
    // ready to be copied into the model's input tensor. Returns false if
    // `length` is 0. Pass the input tensor's quantization parameters
    // (input->params) to match the loaded model exactly.
    bool preprocess(const int16_t* raw, size_t length, uint8_t* output,
                    float scale = kInputScale,
                    int32_t zero_point = kInputZeroPoint);

    // Stage 1: raw counts -> normalized floats. `out` holds `length` samples.
    void Normalize(const int16_t* raw, size_t length, float* out) const;

    // Stage 2: pad `data` (holding `length` samples, capacity at least
    // `target_length`) up to `target_length` samples with, per axis, the most
    // frequent value among the last kPaddingModeWindow samples. Ties go to
    // the most recent value. Returns the new length (unchanged if already
    // long enough or `length` is 0).
    size_t PadWithTailMode(float* data, size_t length, size_t target_length) const;

    // Stage 3: returns the length after dropping trailing samples so that at
    // most `target_length` remain. The data itself needs no change.
    size_t CutTrailing(size_t length, size_t target_length) const;

    // Stage 4: block-average `length` samples down to `out_length` samples
    // (`length` >= `out_length` >= 1). Averaging acts as a simple anti-alias filter.
    void Downsample(const float* in, size_t length, float* out, size_t out_length) const;

    // Stage 5: float -> uint8, round(value / scale) + zero_point clipped to
    // [0, 255]. `count` is the number of values (samples * kNumAxes).
    void Quantize(const float* in, size_t count, uint8_t* out,
                  float scale = kInputScale,
                  int32_t zero_point = kInputZeroPoint) const;

private:
    // Work buffer for the padded/cut window.
    float window_[kInputWindowLength * kNumAxes];
    float downsampled_[kTargetLength * kNumAxes];
};

#endif // DATA_PREPROCESSOR_HPP

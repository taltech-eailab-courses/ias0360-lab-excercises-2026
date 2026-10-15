#include "data_preprocessor.hpp"

#include <algorithm>
#include <cmath>

void DataPreprocessor::Normalize(const int16_t* raw, size_t length, float* out) const {
  for (size_t t = 0; t < length; ++t) {
    for (int axis = 0; axis < kNumAxes; ++axis) {
      const size_t i = t * kNumAxes + axis;
      // Raw sensor counts -> g (zero bias assumed), then training mean/std.
      const float g = static_cast<float>(raw[i]) / kCountsPerG;
      out[i] = (g - kAxisMean[axis]) / kAxisStd[axis];
    }
  }
}

size_t DataPreprocessor::PadWithTailMode(float* data, size_t length, size_t target_length) const {
  if (length == 0 || length >= target_length) {
    return length;
  }

  const size_t tail = std::min<size_t>(length, kPaddingModeWindow);
  const size_t tail_start = length - tail;

  for (int axis = 0; axis < kNumAxes; ++axis) {
    // Equal counts map to bit-identical floats, so exact comparison is safe.
    // Scan from the newest sample and keep only strictly larger counts so
    // ties resolve to the most recent value.
    float mode = data[(length - 1) * kNumAxes + axis];
    size_t best_count = 0;
    for (size_t a = length; a-- > tail_start;) {
      const float candidate = data[a * kNumAxes + axis];
      size_t count = 0;
      for (size_t b = tail_start; b < length; ++b) {
        if (data[b * kNumAxes + axis] == candidate) {
          ++count;
        }
      }
      if (count > best_count) {
        best_count = count;
        mode = candidate;
      }
    }

    for (size_t t = length; t < target_length; ++t) {
      data[t * kNumAxes + axis] = mode;
    }
  }
  return target_length;
}

size_t DataPreprocessor::CutTrailing(size_t length, size_t target_length) const {
  return std::min(length, target_length);
}

void DataPreprocessor::Downsample(const float* in, size_t length, float* out,
                                  size_t out_length) const {
  for (size_t k = 0; k < out_length; ++k) {
    // Bins partition [0, length) so every sample contributes exactly once.
    const size_t begin = k * length / out_length;
    const size_t end = std::max(begin + 1, (k + 1) * length / out_length);
    for (int axis = 0; axis < kNumAxes; ++axis) {
      float sum = 0.0f;
      for (size_t t = begin; t < end; ++t) {
        sum += in[t * kNumAxes + axis];
      }
      out[k * kNumAxes + axis] = sum / static_cast<float>(end - begin);
    }
  }
}

void DataPreprocessor::Quantize(const float* in, size_t count, uint8_t* out, float scale,
                                int32_t zero_point) const {
  for (size_t i = 0; i < count; ++i) {
    const int32_t q = static_cast<int32_t>(std::lround(in[i] / scale)) + zero_point;
    out[i] = static_cast<uint8_t>(std::max<int32_t>(0, std::min<int32_t>(255, q)));
  }
}

bool DataPreprocessor::preprocess(const int16_t* raw, size_t length, uint8_t* output,
                                  float scale, int32_t zero_point) {
  if (raw == nullptr || output == nullptr || length == 0) {
    return false;
  }

  // Samples beyond the window would be cut in stage 3, so only the retained
  // part is normalized; per-sample normalization makes this equivalent and
  // keeps the work buffer at window size.
  size_t n = CutTrailing(length, kInputWindowLength);
  Normalize(raw, n, window_);
  n = PadWithTailMode(window_, n, kInputWindowLength);
  Downsample(window_, n, downsampled_, kTargetLength);
  Quantize(downsampled_, kTargetLength * kNumAxes, output, scale, zero_point);
  return true;
}

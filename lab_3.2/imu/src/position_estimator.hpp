#pragma once
#include <cstddef>
#include <cstdint>

// Longest recording (stationary prefix + gesture) the buffers of the application can hold.
constexpr size_t kMaxRecordingSamples = 8192;

// Position estimation exactly as estimate_trajectory() in visualize_imu_data.ipynb (accelerometer only,
// fixed sensor orientation):
//   1. counts -> m/s^2 (+-2 g range, 16384 LSB/g),
//   2. the mean of the stationary prefix (gravity + bias in the sensor frame) is subtracted,
//   3. 4th-order Butterworth low-pass (cutoff_hz) over prefix and gesture together, started from the
//      steady state of the first sample (sosfilt_zi), then the prefix is cropped,
//   4. trapezoidal integration to velocity, linear detrend so that the final velocity is zero,
//   5. trapezoidal integration to position.
// raw             n x (ax, ay, az) in counts; the first `prefix` samples are taken while the board is still
// position        (n - prefix) x (x, y, z) in metres in the sensor frame, starting at 0
// sample_rate_hz  rate of the raw samples (uniform sampling is assumed, as in the notebook)
// Returns false (position untouched) if the arguments violate the notebook's preconditions.
bool estimate_trajectory(const int16_t (*raw)[3], size_t n, size_t prefix, float sample_rate_hz,
                         float cutoff_hz, float (*position)[3]);

#include "position_estimator.hpp"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.80665;
constexpr double kCountsPerG = 16384.0;   // +-2 g range

struct Biquad {
    double b0, b1, b2, a1, a2;   // a0 = 1
    double z1, z2;               // transposed direct form II state
};

// Section of a 4th-order Butterworth low-pass (bilinear transform with pre-warping, like scipy.signal.butter).
Biquad design_section(double cutoff_hz, double sample_rate_hz, double q) {
    const double w0 = 2.0 * kPi * cutoff_hz / sample_rate_hz;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    Biquad s;
    s.b0 = (1.0 - c) / 2.0 / a0;
    s.b1 = (1.0 - c) / a0;
    s.b2 = s.b0;
    s.a1 = -2.0 * c / a0;
    s.a2 = (1.0 - alpha) / a0;
    s.z1 = s.z2 = 0.0;
    return s;
}

// Steady state for a constant input x (what sosfilt_zi(sos) * x gives for the whole cascade).
double init_steady_state(Biquad& s, double x) {
    const double y = x * (s.b0 + s.b1 + s.b2) / (1.0 + s.a1 + s.a2);
    s.z1 = y - s.b0 * x;
    s.z2 = s.b2 * x - s.a2 * y;
    return y;
}

double step(Biquad& s, double x) {
    const double y = s.b0 * x + s.z1;
    s.z1 = s.b1 * x - s.a1 * y + s.z2;
    s.z2 = s.b2 * x - s.a2 * y;
    return y;
}
}  // namespace

bool estimate_trajectory(const int16_t (*raw)[3], size_t n, size_t prefix, float sample_rate_hz,
                         float cutoff_hz, float (*position)[3]) {
    if (!(sample_rate_hz > 0.0f) || !(cutoff_hz > 0.0f) || !(cutoff_hz < sample_rate_hz / 2.0f)) return false;
    if (prefix < 2 || n < prefix + 3) return false;

    const size_t m = n - prefix;   // gesture samples
    const double fs = sample_rate_hz;
    const double dt = 1.0 / fs;
    const double scale = kGravity / kCountsPerG;
    const double q1 = 1.0 / (2.0 * std::cos(kPi / 8.0));
    const double q2 = 1.0 / (2.0 * std::cos(3.0 * kPi / 8.0));

    for (int axis = 0; axis < 3; axis++) {
        // Gravity plus sensor bias in the sensor frame, from the stationary prefix.
        double reference = 0.0;
        for (size_t i = 0; i < prefix; i++) reference += raw[i][axis];
        reference /= static_cast<double>(prefix);

        Biquad sections[2] = {design_section(cutoff_hz, fs, q1), design_section(cutoff_hz, fs, q2)};
        const double first = (raw[0][axis] - reference) * scale;
        double level = first;
        for (Biquad& s : sections) level = init_steady_state(s, level);

        // Filter prefix and gesture together, keep the gesture. The buffer first holds the acceleration.
        for (size_t i = 0; i < n; i++) {
            double v = (raw[i][axis] - reference) * scale;
            for (Biquad& s : sections) v = step(s, v);
            if (i >= prefix) position[i - prefix][axis] = static_cast<float>(v);
        }

        // Acceleration -> velocity (cumulative trapezoid, zero initial velocity), in place.
        double previous_acc = position[0][axis];
        double velocity = 0.0;
        position[0][axis] = 0.0f;
        for (size_t k = 1; k < m; k++) {
            const double acc = position[k][axis];
            velocity += 0.5 * (previous_acc + acc) * dt;
            previous_acc = acc;
            position[k][axis] = static_cast<float>(velocity);
        }

        // Zero terminal velocity: remove a linear trend. progress = time / time[-1].
        const double final_velocity = velocity;
        for (size_t k = 0; k < m; k++) {
            const double progress = static_cast<double>(k) / static_cast<double>(m - 1);
            position[k][axis] = static_cast<float>(position[k][axis] - progress * final_velocity);
        }

        // Velocity -> position, in place.
        double previous_vel = position[0][axis];
        double pos = 0.0;
        position[0][axis] = 0.0f;
        for (size_t k = 1; k < m; k++) {
            const double vel = position[k][axis];
            pos += 0.5 * (previous_vel + vel) * dt;
            previous_vel = vel;
            position[k][axis] = static_cast<float>(pos);
        }
    }
    return true;
}

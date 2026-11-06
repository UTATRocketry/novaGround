#include "waveform.hpp"

#include <algorithm>
#include <cmath>

namespace {
double smoothstep(double x) {
    x = std::max(0.0, std::min(1.0, x));
    return x * x * (3.0 - 2.0 * x);
}

constexpr double kPi = 3.14159265358979323846;
}

double wave_value(const Waveform& w, double t, WaveformRng& rng) {
    const double lo = w.min;
    const double hi = w.max;
    const double per = std::max(w.period, 1e-6);
    const double mid = (lo + hi) / 2.0;
    const double amp = (hi - lo) / 2.0;
    // Python's t % per is never negative; fmod of a negative t would be.
    double wrapped = std::fmod(t, per);
    if (wrapped < 0.0) wrapped += per;
    const double phase = wrapped / per;  // 0..1 within the period

    double base = mid;
    if (w.wave == "flat") {
        base = w.value;
    } else if (w.wave == "sine") {
        base = mid + amp * std::sin(2.0 * kPi * phase);
    } else if (w.wave == "square") {
        base = phase < 0.5 ? hi : lo;
    } else if (w.wave == "triangle") {
        base = lo + (hi - lo) * (phase < 0.5 ? 2.0 * phase : 2.0 * (1.0 - phase));
    } else if (w.wave == "sawtooth") {
        base = lo + (hi - lo) * phase;
    } else if (w.wave == "s_curve") {
        base = lo + (hi - lo) * smoothstep(phase);
    } else if (w.wave == "ramp") {
        base = lo + (hi - lo) * std::min(t / per, 1.0);
    } else if (w.wave == "noise") {
        base = rng.uniform(std::min(lo, hi), std::max(lo, hi));
    }

    if (w.noise != 0.0) {
        const double n = std::fabs(w.noise);
        base += rng.uniform(-n, n);
    }
    return base;
}

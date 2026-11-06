#pragma once

#include <cstdint>
#include <random>
#include <string>

// The sensor waveform model of the Python simulator (tools/novaSim.py in the
// backend), so both simulators can be driven by the same scenario values.
// Names: flat, sine, square, triangle, sawtooth, s_curve, ramp, noise.
struct Waveform {
    std::string wave = "flat";
    double min = 0.0;
    double max = 1.0;
    double period = 10.0;  // seconds
    double noise = 0.0;    // +/- uniform noise added to the value
    double value = 0.5;    // the level of a "flat" wave

    static Waveform flat(double level) {
        Waveform w;
        w.wave = "flat";
        w.min = w.max = w.value = level;
        return w;
    }
};

// Deterministic given the seed. `noise` waves and noise amplitude draw from it.
class WaveformRng {
public:
    explicit WaveformRng(uint32_t seed = 1) : engine_(seed) {}
    double uniform(double lo, double hi) {
        return std::uniform_real_distribution<double>(lo, hi)(engine_);
    }

private:
    std::mt19937 engine_;
};

// The value at `t` seconds after the simulator started. "ramp" is a one-shot
// ramp from min to max over `period`, then holds at max. An unknown wave name
// gives the midpoint, as in Python.
double wave_value(const Waveform& w, double t, WaveformRng& rng);

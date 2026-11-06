#include <boost/test/unit_test.hpp>
#include <boost/test/tools/floating_point_comparison.hpp>

#include "sim/waveform.hpp"

namespace tt = boost::test_tools;

namespace {
Waveform make(const char* name, double lo, double hi, double period) {
    Waveform w;
    w.wave = name;
    w.min = lo;
    w.max = hi;
    w.period = period;
    w.value = (lo + hi) / 2.0;
    return w;
}

struct Fixture {
    WaveformRng rng{1};
};
}

BOOST_FIXTURE_TEST_SUITE(waveform, Fixture)

BOOST_AUTO_TEST_CASE(flat_is_its_value) {
    Waveform w = Waveform::flat(3.5);
    BOOST_TEST(wave_value(w, 0.0, rng) == 3.5, tt::tolerance(1e-12));
    BOOST_TEST(wave_value(w, 123.4, rng) == 3.5, tt::tolerance(1e-12));
}

BOOST_AUTO_TEST_CASE(sine_starts_at_the_midpoint_and_peaks_a_quarter_period_in) {
    Waveform w = make("sine", 1.0, 5.0, 20.0);
    BOOST_TEST(wave_value(w, 0.0, rng) == 3.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 5.0, rng) == 5.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 15.0, rng) == 1.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 20.0, rng) == 3.0, tt::tolerance(1e-9));
}

BOOST_AUTO_TEST_CASE(square_is_high_for_the_first_half_period) {
    Waveform w = make("square", 1.0, 5.0, 10.0);
    BOOST_TEST(wave_value(w, 1.0, rng) == 5.0);
    BOOST_TEST(wave_value(w, 6.0, rng) == 1.0);
    BOOST_TEST(wave_value(w, 11.0, rng) == 5.0);
}

BOOST_AUTO_TEST_CASE(triangle_rises_then_falls) {
    Waveform w = make("triangle", 0.0, 10.0, 8.0);
    BOOST_TEST(wave_value(w, 0.0, rng) == 0.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 2.0, rng) == 5.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 4.0, rng) == 10.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 6.0, rng) == 5.0, tt::tolerance(1e-9));
}

BOOST_AUTO_TEST_CASE(sawtooth_rises_and_resets) {
    Waveform w = make("sawtooth", 0.0, 10.0, 10.0);
    BOOST_TEST(wave_value(w, 5.0, rng) == 5.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 10.0, rng) == 0.0, tt::tolerance(1e-9));
}

BOOST_AUTO_TEST_CASE(s_curve_is_a_smoothstep) {
    Waveform w = make("s_curve", 0.0, 10.0, 10.0);
    BOOST_TEST(wave_value(w, 0.0, rng) == 0.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 5.0, rng) == 5.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 2.5, rng) == 10.0 * (0.25 * 0.25 * (3.0 - 0.5)), tt::tolerance(1e-9));
}

BOOST_AUTO_TEST_CASE(ramp_is_one_shot_and_holds_at_the_top) {
    Waveform w = make("ramp", 2.0, 4.0, 10.0);
    BOOST_TEST(wave_value(w, 0.0, rng) == 2.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 5.0, rng) == 3.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 10.0, rng) == 4.0, tt::tolerance(1e-9));
    BOOST_TEST(wave_value(w, 25.0, rng) == 4.0, tt::tolerance(1e-9));
}

BOOST_AUTO_TEST_CASE(noise_wave_stays_within_min_and_max) {
    Waveform w = make("noise", -1.0, 3.0, 10.0);
    for (int i = 0; i < 500; ++i) {
        double v = wave_value(w, i * 0.1, rng);
        BOOST_REQUIRE(v >= -1.0);
        BOOST_REQUIRE(v <= 3.0);
    }
}

BOOST_AUTO_TEST_CASE(an_unknown_wave_gives_the_midpoint) {
    Waveform w = make("zigzag", 2.0, 6.0, 10.0);
    BOOST_TEST(wave_value(w, 3.0, rng) == 4.0, tt::tolerance(1e-12));
}

BOOST_AUTO_TEST_CASE(added_noise_stays_within_its_amplitude) {
    Waveform w = make("flat", 0.0, 0.0, 10.0);
    w.value = 10.0;
    w.noise = 0.5;
    bool moved = false;
    for (int i = 0; i < 500; ++i) {
        double v = wave_value(w, 0.0, rng);
        BOOST_REQUIRE(v >= 9.5);
        BOOST_REQUIRE(v <= 10.5);
        if (v != 10.0) moved = true;
    }
    BOOST_TEST(moved);
}

BOOST_AUTO_TEST_CASE(the_same_seed_gives_the_same_noise) {
    Waveform w = make("noise", 0.0, 1.0, 10.0);
    WaveformRng a(7), b(7), c(8);
    double va = wave_value(w, 0.0, a);
    BOOST_TEST(wave_value(w, 0.0, b) == va);
    BOOST_TEST(wave_value(w, 0.0, c) != va);
}

BOOST_AUTO_TEST_CASE(a_tiny_or_zero_period_and_negative_time_stay_finite) {
    Waveform w = make("sine", 0.0, 1.0, 0.0);
    double v = wave_value(w, 0.3, rng);
    BOOST_TEST(v >= 0.0);
    BOOST_TEST(v <= 1.0);
    Waveform s = make("sawtooth", 0.0, 10.0, 10.0);
    BOOST_TEST(wave_value(s, -2.0, rng) == 8.0, tt::tolerance(1e-9));
}

BOOST_AUTO_TEST_SUITE_END()

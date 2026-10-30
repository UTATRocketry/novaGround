#include <boost/test/unit_test.hpp>

#include <chrono>
#include <functional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "controllers/sampler.hpp"
#include "fake_digital_io.hpp"
#include "utils/interruptible_sleep.hpp"

using namespace std::chrono;

namespace {
class FakeAnalogIo : public AnalogIo {
public:
    std::vector<DaqHatDevice> discover() override { return {}; }
    bool open(const DaqHatDevice&) override { return true; }
    bool read(const DaqHatDevice&, int, double& value) override {
        value = 1.0;
        return true;
    }
};

// True once `ready` holds, false if it does not within the deadline.
bool wait_until(const std::function<bool()>& ready, milliseconds deadline = seconds(5)) {
    auto end = steady_clock::now() + deadline;
    while (steady_clock::now() < end) {
        if (ready()) return true;
        std::this_thread::sleep_for(milliseconds(1));
    }
    return ready();
}

DaqHatDevice one_channel_hat() {
    DaqHatDevice h;
    h.address = 0;
    h.hat_id = 1;
    h.channels = {0};
    return h;
}

// A long interval must not delay the stop: that is what the interruptible
// sleep is for.
constexpr int kOneHourMs = 3600 * 1000;
constexpr long long kPromptlyMs = 2000;

long long ms_since(steady_clock::time_point start) {
    return duration_cast<milliseconds>(steady_clock::now() - start).count();
}
}

BOOST_AUTO_TEST_SUITE(stoppable_loops)

BOOST_AUTO_TEST_CASE(sleep_runs_its_course_when_no_stop_is_requested) {
    InterruptibleSleep sleeper;
    std::stop_source source;
    auto start = steady_clock::now();
    BOOST_TEST(!sleeper.sleep_for(source.get_token(), milliseconds(30)));
    BOOST_TEST(ms_since(start) >= 25);
}

BOOST_AUTO_TEST_CASE(sleep_returns_at_once_if_a_stop_was_already_requested) {
    InterruptibleSleep sleeper;
    std::stop_source source;
    source.request_stop();
    auto start = steady_clock::now();
    BOOST_TEST(sleeper.sleep_for(source.get_token(), hours(1)));
    BOOST_TEST(ms_since(start) < kPromptlyMs);
}

BOOST_AUTO_TEST_CASE(a_stop_request_cuts_a_long_sleep_short) {
    InterruptibleSleep sleeper;
    std::stop_source source;
    std::thread stopper([&source] {
        std::this_thread::sleep_for(milliseconds(50));
        source.request_stop();
    });
    auto start = steady_clock::now();
    BOOST_TEST(sleeper.sleep_for(source.get_token(), hours(1)));
    BOOST_TEST(ms_since(start) < kPromptlyMs);
    stopper.join();
}

BOOST_AUTO_TEST_CASE(the_daq_loop_samples_then_stops_promptly) {
    FakeAnalogIo daq;
    TelemetryStore store;
    std::vector<DaqHatDevice> hats{one_channel_hat()};

    std::jthread loop(daq_sample_loop, std::ref(static_cast<AnalogIo&>(daq)), hats,
                      std::ref(store), static_cast<DataLogger*>(nullptr), kOneHourMs);
    BOOST_REQUIRE(wait_until([&] { return !store.snapshot_sensors().empty(); }));

    auto start = steady_clock::now();
    loop.request_stop();
    loop.join();
    BOOST_TEST(ms_since(start) < kPromptlyMs);
}

BOOST_AUTO_TEST_CASE(the_daq_loop_keeps_sampling_until_stopped) {
    FakeAnalogIo daq;
    TelemetryStore store;
    std::vector<DaqHatDevice> hats{one_channel_hat()};
    double first_pass = 0;

    std::jthread loop(daq_sample_loop, std::ref(static_cast<AnalogIo&>(daq)), hats,
                      std::ref(store), static_cast<DataLogger*>(nullptr), 1);
    BOOST_REQUIRE(wait_until([&] { return !store.snapshot_sensors().empty(); }));
    first_pass = store.snapshot_sensors().at(0).timestamp_ms;
    // A later pass has a later timestamp.
    BOOST_TEST(wait_until([&] { return store.snapshot_sensors().at(0).timestamp_ms > first_pass; }));
}

BOOST_AUTO_TEST_CASE(the_gpio_loop_samples_then_stops_promptly) {
    FakeDigitalIo io;
    io.set_direction(5, "in");
    io.levels[5] = 1;
    TelemetryStore store;

    std::jthread loop(gpio_sample_loop, std::ref(static_cast<DigitalIo&>(io)), std::ref(store),
                      kOneHourMs);
    BOOST_REQUIRE(wait_until([&] { return !store.snapshot_gpio_states().empty(); }));

    auto start = steady_clock::now();
    loop.request_stop();
    loop.join();
    BOOST_TEST(ms_since(start) < kPromptlyMs);
}

BOOST_AUTO_TEST_CASE(a_loop_given_an_already_stopped_token_does_no_work) {
    FakeDigitalIo io;
    io.set_direction(5, "in");
    TelemetryStore store;
    std::stop_source source;
    source.request_stop();
    gpio_sample_loop(source.get_token(), io, store, 1);  // returns at once
    BOOST_TEST(store.snapshot_gpio_states().empty());
}

BOOST_AUTO_TEST_SUITE_END()

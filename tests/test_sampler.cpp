#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "controllers/sampler.hpp"
#include "fake_digital_io.hpp"
#include "temp_dir.hpp"

namespace {
class FakeAnalogIo : public AnalogIo {
public:
    std::map<std::pair<int, int>, double> values;  // (address, channel) -> value
    std::set<std::pair<int, int>> failing;
    std::vector<std::pair<int, int>> reads;

    std::vector<DaqHatDevice> discover() override { return {}; }
    bool open(const DaqHatDevice&) override { return true; }
    bool read(const DaqHatDevice& hat, int channel, double& value) override {
        reads.push_back({hat.address, channel});
        if (failing.count({hat.address, channel})) return false;
        value = values[{hat.address, channel}];
        return true;
    }
};

DaqHatDevice hat(int address, std::vector<int> channels) {
    DaqHatDevice h;
    h.address = address;
    h.hat_id = 1;
    h.channels = std::move(channels);
    return h;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) out.push_back(l);
    return out;
}

struct Fixture {
    FakeAnalogIo daq;
    TelemetryStore store;
};
}

BOOST_FIXTURE_TEST_SUITE(sampler, Fixture)

BOOST_AUTO_TEST_CASE(headers_are_timestamp_then_hat_and_channel) {
    auto h = build_sensor_headers({hat(0, {0, 1}), hat(3, {2})});
    BOOST_TEST((h == std::vector<std::string>{"timestamp", "hat0_ch0", "hat0_ch1", "hat3_ch2"}));
}

BOOST_AUTO_TEST_CASE(a_hat_without_channels_adds_no_headers) {
    auto h = build_sensor_headers({hat(0, {}), hat(1, {0})});
    BOOST_TEST((h == std::vector<std::string>{"timestamp", "hat1_ch0"}));
    BOOST_TEST((build_sensor_headers({}) == std::vector<std::string>{"timestamp"}));
}

BOOST_AUTO_TEST_CASE(every_channel_is_read_once_in_hat_then_channel_order) {
    daq.values = {{{0, 0}, 1.5}, {{0, 1}, 2.5}, {{1, 0}, 3.5}};
    daq_sample_once(daq, {hat(0, {0, 1}), hat(1, {0})}, store, nullptr);

    using Reads = std::vector<std::pair<int, int>>;
    BOOST_TEST((daq.reads == Reads{{0, 0}, {0, 1}, {1, 0}}));

    auto s = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(s.size(), 3u);
    BOOST_TEST(s[0].hat_id == 0);
    BOOST_TEST(s[0].channel_id == 0);
    BOOST_TEST(s[0].value == 1.5);
    BOOST_TEST(s[1].channel_id == 1);
    BOOST_TEST(s[1].value == 2.5);
    BOOST_TEST(s[2].hat_id == 1);
    BOOST_TEST(s[2].value == 3.5);
}

BOOST_AUTO_TEST_CASE(hat_id_in_a_sample_is_the_hat_address) {
    daq_sample_once(daq, {hat(5, {0})}, store, nullptr);
    BOOST_TEST(store.snapshot_sensors().at(0).hat_id == 5);
}

BOOST_AUTO_TEST_CASE(all_samples_of_one_pass_share_a_current_timestamp) {
    using namespace std::chrono;
    auto ms = [] { return double(duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()); };
    double before = ms();
    daq_sample_once(daq, {hat(0, {0, 1, 2})}, store, nullptr);
    double after = ms();
    auto s = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(s.size(), 3u);
    BOOST_TEST(s[0].timestamp_ms >= before);
    BOOST_TEST(s[0].timestamp_ms <= after);
    BOOST_TEST(s[1].timestamp_ms == s[0].timestamp_ms);
    BOOST_TEST(s[2].timestamp_ms == s[0].timestamp_ms);
}

BOOST_AUTO_TEST_CASE(a_failed_read_is_nan_and_the_rest_still_sample) {
    daq.values = {{{0, 0}, 1.0}, {{0, 2}, 3.0}};
    daq.failing.insert({0, 1});
    daq_sample_once(daq, {hat(0, {0, 1, 2})}, store, nullptr);
    auto s = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(s.size(), 3u);
    BOOST_TEST(s[0].value == 1.0);
    BOOST_TEST(std::isnan(s[1].value));
    BOOST_TEST(s[2].value == 3.0);
}

BOOST_AUTO_TEST_CASE(no_hats_publish_an_empty_sample_set) {
    store.set_sensors({{9, 9, 9.0, 9.0}});
    daq_sample_once(daq, {}, store, nullptr);
    BOOST_TEST(store.snapshot_sensors().empty());
}

BOOST_AUTO_TEST_CASE(a_new_pass_replaces_the_previous_one) {
    daq.values = {{{0, 0}, 1.0}};
    daq_sample_once(daq, {hat(0, {0})}, store, nullptr);
    daq.values = {{{0, 0}, 2.0}};
    daq_sample_once(daq, {hat(0, {0})}, store, nullptr);
    auto s = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(s.size(), 1u);
    BOOST_TEST(s[0].value == 2.0);
}

BOOST_AUTO_TEST_CASE(rows_are_logged_only_while_recording_and_in_header_order) {
    TempDir dir;
    std::vector<DaqHatDevice> hats{hat(0, {0, 1}), hat(1, {0})};
    DataLogger logger(dir.path().string(), build_sensor_headers(hats), {"timestamp"}, {}, "n");
    daq.values = {{{0, 0}, 1.0}, {{0, 1}, 2.0}, {{1, 0}, 3.0}};

    daq_sample_once(daq, hats, store, &logger);  // not recording: no row
    BOOST_REQUIRE(logger.start("r"));
    daq_sample_once(daq, hats, store, &logger);
    daq.failing.insert({0, 1});
    daq_sample_once(daq, hats, store, &logger);
    logger.stop();

    auto l = lines(read_file(logger.last_sensor_path()));
    BOOST_REQUIRE_EQUAL(l.size(), 3u);  // header + two rows
    BOOST_TEST(l[0] == "timestamp,hat0_ch0,hat0_ch1,hat1_ch0");
    BOOST_TEST(l[1].substr(l[1].find(',')) == ",1,2,3");
    BOOST_TEST(l[2].substr(l[2].find(',')) == ",1,NaN,3");
}

BOOST_AUTO_TEST_CASE(gpio_sampling_stores_the_input_pins_only) {
    FakeDigitalIo io;
    io.set_direction(5, "in");
    io.set_direction(6, "in");
    io.set_direction(17, "out");
    io.levels = {{5, 1}, {6, 0}, {17, 1}};
    gpio_sample_once(io, store);
    auto states = store.snapshot_gpio_states();
    BOOST_TEST(states.size() == 2u);
    BOOST_TEST(states.at(5) == 1);
    BOOST_TEST(states.at(6) == 0);
}

BOOST_AUTO_TEST_CASE(gpio_sampling_replaces_the_previous_states) {
    FakeDigitalIo io;
    io.set_direction(5, "in");
    io.levels = {{5, 1}};
    gpio_sample_once(io, store);
    io.levels = {{5, 0}};
    gpio_sample_once(io, store);
    BOOST_TEST(store.snapshot_gpio_states().at(5) == 0);
}

BOOST_AUTO_TEST_CASE(a_throwing_gpio_read_is_swallowed_and_keeps_the_old_states) {
    FakeDigitalIo io;
    io.set_direction(5, "in");
    io.levels = {{5, 1}};
    gpio_sample_once(io, store);
    io.throw_on_read_all = true;
    BOOST_CHECK_NO_THROW(gpio_sample_once(io, store));
    BOOST_TEST(store.snapshot_gpio_states().at(5) == 1);
}

BOOST_AUTO_TEST_SUITE_END()

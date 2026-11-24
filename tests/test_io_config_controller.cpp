#include <boost/test/unit_test.hpp>

#include <boost/json.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "controllers/gpio_controller.hpp"
#include "controllers/io_config_controller.hpp"
#include "core/data_logger.hpp"
#include "core/output_supervisor.hpp"
#include "drivers/mcc_daqhats.hpp"
#include "fake_digital_io.hpp"
#include "sim/daqhats_sim.hpp"
#include "sim/scenario.hpp"
#include "temp_dir.hpp"

#include "daqhats/mcc134.h"

namespace json = boost::json;
using namespace std::chrono;

namespace {
class FakeTcDaq : public AnalogIo {
public:
    std::vector<std::tuple<int, int, TcType>> writes;
    std::set<std::pair<int, int>> failing;

    std::vector<DaqHatDevice> discover() override { return {}; }
    bool open(const DaqHatDevice&) override { return true; }
    bool read(const DaqHatDevice&, int, double& value) override {
        value = 0;
        return true;
    }
    bool set_tc_type(int address, int channel, TcType type) override {
        if (failing.count({address, channel})) return false;
        writes.emplace_back(address, channel, type);
        return true;
    }
};

DaqHatDevice mcc134(int address) {
    DaqHatDevice hat;
    hat.address = address;
    hat.channels = {0, 1, 2, 3};
    hat.thermocouples = true;
    return hat;
}

DaqHatDevice mcc128(int address) {
    DaqHatDevice hat;
    hat.address = address;
    hat.channels = {0, 1, 2, 3, 4, 5, 6, 7};
    return hat;
}

const char* kExample = R"({
  "config_hash": "3f9a1c0be27d4a55",
  "gpio": {
    "outputs": {
      "17": { "max_on_ms": 1800000, "on_comms_loss": "safe" },
      "27": { "max_on_ms": 600000, "on_comms_loss": "safe" },
      "8": {}
    },
    "inputs": [5]
  },
  "relays": { "3": { "max_on_ms": null, "on_comms_loss": "hold" } },
  "daq": { "1": { "0": "K", "1": "T" } },
  "heartbeat": { "timeout_ms": 2000 }
})";

struct Fixture {
    steady_clock::time_point now{seconds(1000)};
    FakeDigitalIo io;
    FakeTcDaq daq;
    OutputSupervisor supervisor{[this] { return now; }, [] { return std::int64_t{1795110123456}; }};
    SupervisedDigitalIo supervised{io, supervisor};
    GpioController gpio{&supervised, nullptr};
    IoConfigController config{&gpio, &daq, {mcc128(0), mcc134(1)}, &supervisor, nullptr,
                              [] { return std::int64_t{1795110000000}; }};
    std::vector<std::string> published;

    Fixture() {
        gpio.set_supervisor(&supervisor);
        supervisor.set_trip_handler([this](const OutputId& o, const std::string& r) { gpio.drive_safe(o.index, r); });
        config.set_publisher([this](const std::string& s) { published.push_back(s); });
    }

    json::object status() { return json::parse(config.status_json()).as_object(); }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(io_config_controller, Fixture)

BOOST_AUTO_TEST_CASE(before_any_config_the_status_says_so) {
    auto s = status();
    BOOST_TEST(s.at("config_hash").is_null());
    BOOST_TEST(s.at("applied_ms").is_null());
    BOOST_TEST(s.at("rejected").as_array().empty());
    BOOST_TEST(s.at("tripped").as_array().empty());
    BOOST_TEST(json::serialize(s.at("reserved_pins")) == "[0,1,2,3,7,8,9,10,11,12,13,16,20,21,26]");
    // Every MCC 134 channel at K; the MCC 128 has no thermocouples.
    BOOST_TEST(json::serialize(s.at("thermocouples")) == R"({"1":{"0":"K","1":"K","2":"K","3":"K"}})");
    BOOST_TEST(io.calls.empty());
}

BOOST_AUTO_TEST_CASE(the_status_has_exactly_the_design_docs_keys) {
    config.handle_message(kExample);
    auto s = status();
    std::vector<std::string> keys;
    for (const auto& [k, v] : s) keys.emplace_back(k);
    std::sort(keys.begin(), keys.end());
    BOOST_TEST((keys == std::vector<std::string>{"applied_ms", "config_hash", "rejected", "reserved_pins",
                                                   "thermocouples", "tripped"}));
}

BOOST_AUTO_TEST_CASE(a_config_is_applied_and_published) {
    config.handle_message(kExample);
    BOOST_REQUIRE_EQUAL(published.size(), 1u);
    auto s = json::parse(published.back()).as_object();
    BOOST_TEST(s.at("config_hash").as_string() == "3f9a1c0be27d4a55");
    BOOST_TEST(s.at("applied_ms").to_number<std::int64_t>() == 1795110000000);
    BOOST_TEST(json::serialize(s.at("rejected")) == R"x([{"item":"gpio.outputs.8","reason":"reserved (SPI0 CE0)"}])x");
    BOOST_TEST(json::serialize(s.at("thermocouples")) == R"({"1":{"0":"K","1":"T","2":"K","3":"K"}})");

    BOOST_TEST((gpio.output_pins() == std::vector<int>{17, 27}));
    BOOST_TEST((gpio.input_pins() == std::vector<int>{5}));
    BOOST_TEST(io.levels[17] == 0);
    // Only the change was written.
    BOOST_REQUIRE_EQUAL(daq.writes.size(), 1u);
    BOOST_TEST((daq.writes[0] == std::make_tuple(1, 1, TcType::T)));
}

BOOST_AUTO_TEST_CASE(the_limits_reach_the_supervisor) {
    config.handle_message(kExample);
    supervisor.heartbeat(R"({"online":true,"seq":1})", false);
    gpio.handle_command(json::parse(R"({"id":17,"state":true})").as_object());
    BOOST_TEST(io.levels[17] == 1);
    supervisor.connection_lost();
    supervisor.tick();
    BOOST_TEST(io.levels[17] == 0);
    auto s = status();
    BOOST_TEST(json::serialize(s.at("tripped")) ==
               R"([{"output":"gpio:17","reason":"comms_loss","time_ms":1795110123456}])");
}

BOOST_AUTO_TEST_CASE(a_channel_dropped_from_the_config_goes_back_to_the_default_type) {
    config.handle_message(kExample);
    daq.writes.clear();
    config.handle_message(R"({"config_hash":"b","daq":{"1":{"2":"J"}}})");
    BOOST_TEST(json::serialize(status().at("thermocouples")) == R"({"1":{"0":"K","1":"K","2":"J","3":"K"}})");
    BOOST_TEST(daq.writes.size() == 2u);  // 1 back to K, 2 to J
}

BOOST_AUTO_TEST_CASE(a_failed_type_write_is_rejected_and_the_old_type_reported) {
    daq.failing.insert({1, 1});
    config.handle_message(kExample);
    auto s = status();
    BOOST_TEST(json::serialize(s.at("thermocouples")) == R"({"1":{"0":"K","1":"K","2":"K","3":"K"}})");
    bool found = false;
    for (const auto& r : s.at("rejected").as_array()) {
        found = found || r.as_object().at("item").as_string() == "daq.1.1";
    }
    BOOST_TEST(found);
}

BOOST_AUTO_TEST_CASE(a_claim_failure_is_reported_as_rejected) {
    io.failing_claims.insert(27);
    config.handle_message(kExample);
    auto rejected = json::serialize(status().at("rejected"));
    BOOST_TEST(rejected.find("gpio.outputs.27") != std::string::npos);
    BOOST_TEST((gpio.output_pins() == std::vector<int>{17}));
}

BOOST_AUTO_TEST_CASE(an_unusable_message_changes_nothing_applied) {
    config.handle_message(kExample);
    io.calls.clear();
    daq.writes.clear();
    config.handle_message("not json");
    auto s = status();
    BOOST_TEST(s.at("config_hash").as_string() == "3f9a1c0be27d4a55");  // still the applied one
    BOOST_TEST(json::serialize(s.at("rejected")).find("(message)") != std::string::npos);
    BOOST_TEST(io.calls.empty());
    BOOST_TEST(daq.writes.empty());
    BOOST_TEST((gpio.output_pins() == std::vector<int>{17, 27}));
    BOOST_TEST(published.size() == 2u);
}

BOOST_AUTO_TEST_CASE(a_later_config_replaces_the_rejections_and_drops_pins) {
    config.handle_message(kExample);
    config.handle_message(R"({"config_hash":"c","gpio":{"outputs":{"27":{}}}})");
    auto s = status();
    BOOST_TEST(s.at("config_hash").as_string() == "c");
    BOOST_TEST(s.at("rejected").as_array().empty());
    BOOST_TEST((gpio.output_pins() == std::vector<int>{27}));
    BOOST_TEST(gpio.input_pins().empty());
    BOOST_TEST(!io.directions.count(17));
}

BOOST_AUTO_TEST_CASE(the_next_recordings_columns_are_the_configured_outputs) {
    TempDir dir;
    DataLogger logger(dir.path().string(), {"timestamp"}, actuator_log_headers({}), {}, "n");
    IoConfigController with_logger(&gpio, &daq, {}, &supervisor, &logger);

    BOOST_REQUIRE(logger.start("before"));
    with_logger.handle_message(R"({"gpio":{"outputs":{"17":{},"22":{}}}})");
    logger.log_actuator_snapshot("x");
    logger.stop();
    std::string before = read_file(logger.last_actuator_path());
    BOOST_TEST(before.substr(0, before.find('\n')).find("gpio_") == std::string::npos);

    BOOST_REQUIRE(logger.start("after"));
    logger.stop();
    std::string after = read_file(logger.last_actuator_path());
    BOOST_TEST(after.substr(0, after.find('\n')).find("timestamp,gpio_17,gpio_22,relay_0") == 0u);
}

// The real MCC driver over the simulated library: the types reach libdaqhats.
BOOST_AUTO_TEST_CASE(types_reach_the_library_through_the_mcc_driver) {
    daqhats_sim::load(parse_scenario(R"({"hats":[{"address":1,"type":"mcc134","channels":{}}]})"));
    MccDaqHats mcc;
    auto hats = discover_and_open(mcc);
    BOOST_REQUIRE_EQUAL(hats.size(), 1u);
    BOOST_TEST(hats[0].thermocouples);
    BOOST_TEST(daqhats_sim::tc_type(1, 1) == TC_TYPE_K);  // open sets K

    IoConfigController real(nullptr, &mcc, hats, nullptr, nullptr);
    real.handle_message(R"({"daq":{"1":{"1":"T","3":"N"}}})");
    BOOST_TEST(daqhats_sim::tc_type(1, 1) == TC_TYPE_T);
    BOOST_TEST(daqhats_sim::tc_type(1, 3) == TC_TYPE_N);
    BOOST_TEST(daqhats_sim::tc_type(1, 0) == TC_TYPE_K);
    BOOST_TEST(!mcc.set_tc_type(2, 0, TcType::K));  // no MCC 134 there
    daqhats_sim::reset();
}

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>

#include <vector>

#include "controllers/relay_controller.hpp"
#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "fake_i2c_bus.hpp"
#include "interfaces/io_expander.hpp"
#include "temp_dir.hpp"

namespace json = boost::json;

namespace {
class FakeRelayBank : public RelayBank {
public:
    bool ready = true;
    bool write_ok = true;
    std::vector<std::bitset<16>> writes;

    bool is_ready() const override { return ready; }
    bool write_output(const std::bitset<16>& state) override {
        writes.push_back(state);
        return write_ok;
    }
};

json::object cmd(const char* text) {
    return json::parse(text).as_object();
}

struct Fixture {
    FakeRelayBank bank;
    TelemetryStore telemetry;
    RelayController controller{&bank, &telemetry, nullptr};
};
}

BOOST_FIXTURE_TEST_SUITE(relay_controller, Fixture)

BOOST_AUTO_TEST_CASE(turning_a_relay_on_writes_and_publishes_the_state) {
    controller.handle_command(cmd(R"({"type":"relay","id":3,"state":true})"));
    BOOST_REQUIRE_EQUAL(bank.writes.size(), 1u);
    BOOST_TEST(bank.writes[0].to_ulong() == 1ul << 3);
    BOOST_TEST(telemetry.snapshot_relay_state().to_ulong() == 1ul << 3);
}

BOOST_AUTO_TEST_CASE(states_accumulate_and_clear_per_relay) {
    controller.handle_command(cmd(R"({"id":1,"state":true})"));
    controller.handle_command(cmd(R"({"id":9,"state":true})"));
    controller.handle_command(cmd(R"({"id":1,"state":false})"));
    BOOST_REQUIRE_EQUAL(bank.writes.size(), 3u);
    BOOST_TEST(bank.writes[1].to_ulong() == ((1ul << 1) | (1ul << 9)));
    BOOST_TEST(bank.writes[2].to_ulong() == 1ul << 9);
}

BOOST_AUTO_TEST_CASE(state_accepts_numbers_like_the_rest_of_the_protocol) {
    controller.handle_command(cmd(R"({"id":0,"state":1})"));
    BOOST_REQUIRE_EQUAL(bank.writes.size(), 1u);
    BOOST_TEST(bank.writes[0].test(0));
}

BOOST_AUTO_TEST_CASE(out_of_range_ids_write_nothing) {
    controller.handle_command(cmd(R"({"id":-1,"state":true})"));
    controller.handle_command(cmd(R"({"id":16,"state":true})"));
    controller.handle_command(cmd(R"({"id":1000,"state":true})"));
    BOOST_TEST(bank.writes.empty());
    BOOST_TEST(telemetry.snapshot_relay_state().none());
}

BOOST_AUTO_TEST_CASE(boundary_ids_0_and_15_are_valid) {
    controller.handle_command(cmd(R"({"id":0,"state":true})"));
    controller.handle_command(cmd(R"({"id":15,"state":true})"));
    BOOST_TEST(bank.writes.size() == 2u);
    BOOST_TEST(bank.writes[1].to_ulong() == ((1ul << 0) | (1ul << 15)));
}

BOOST_AUTO_TEST_CASE(missing_or_wrong_typed_fields_write_nothing) {
    controller.handle_command(cmd(R"({"state":true})"));
    controller.handle_command(cmd(R"({"id":2})"));
    controller.handle_command(cmd(R"({"id":"2","state":true})"));
    controller.handle_command(cmd(R"({"id":2,"state":"on"})"));
    BOOST_TEST(bank.writes.empty());
}

BOOST_AUTO_TEST_CASE(a_failed_write_does_not_publish_the_state) {
    bank.write_ok = false;
    controller.handle_command(cmd(R"({"id":4,"state":true})"));
    BOOST_TEST(bank.writes.size() == 1u);
    BOOST_TEST(telemetry.snapshot_relay_state().none());
}

BOOST_AUTO_TEST_CASE(unavailable_hardware_writes_nothing) {
    bank.ready = false;
    controller.handle_command(cmd(R"({"id":4,"state":true})"));
    BOOST_TEST(bank.writes.empty());

    RelayController none(nullptr, &telemetry, nullptr);
    none.handle_command(cmd(R"({"id":4,"state":true})"));
    BOOST_TEST(telemetry.snapshot_relay_state().none());
}

BOOST_AUTO_TEST_CASE(set_initial_state_seeds_the_next_write_and_telemetry) {
    std::bitset<16> initial(0xFFFF);
    controller.set_initial_state(initial);
    BOOST_TEST(telemetry.snapshot_relay_state() == initial);
    controller.handle_command(cmd(R"({"id":0,"state":false})"));
    BOOST_REQUIRE_EQUAL(bank.writes.size(), 1u);
    BOOST_TEST(bank.writes[0].to_ulong() == 0xFFFEul);
}

BOOST_AUTO_TEST_CASE(the_actuator_log_gets_a_relay_row) {
    TempDir dir;
    std::vector<std::string> headers{"timestamp"};
    for (int i = 0; i < 16; ++i) headers.push_back("relay_" + std::to_string(i));
    headers.push_back("type_id");
    DataLogger logger(dir.path().string(), {"timestamp"}, headers, {}, "n");
    BOOST_REQUIRE(logger.start("r"));
    RelayController logged(&bank, &telemetry, &logger);
    logged.handle_command(cmd(R"({"id":2,"state":true})"));
    logger.stop();
    std::string text = read_file(logger.last_actuator_path());
    BOOST_TEST(text.find(",relay\n") != std::string::npos);
}

// The real chip driver behind the controller, on a fake bus: a command ends up
// as the right register bytes.
BOOST_AUTO_TEST_CASE(controller_to_register_bytes_through_the_real_driver) {
    FakeI2cBus bus;
    TCA9535 chip(bus, 0x20, std::chrono::milliseconds(0));
    RelayController real(&chip, &telemetry, nullptr);
    real.handle_command(cmd(R"({"id":10,"state":true})"));
    BOOST_TEST(bus.reg(0x20, 0x02) == 0x00);
    BOOST_TEST(bus.reg(0x20, 0x03) == 0x04);
}

BOOST_AUTO_TEST_SUITE_END()

#include <boost/test/unit_test.hpp>

#include <atomic>
#include <thread>
#include <vector>

#include "core/telemetry.hpp"

BOOST_AUTO_TEST_SUITE(telemetry)

BOOST_AUTO_TEST_CASE(starts_empty) {
    TelemetryStore store;
    BOOST_TEST(store.snapshot_sensors().empty());
    BOOST_TEST(store.snapshot_gpio_states().empty());
    BOOST_TEST(store.snapshot_relay_state().none());
    BOOST_TEST(store.snapshot_servo_states().empty());
}

BOOST_AUTO_TEST_CASE(sensors_are_replaced_not_appended) {
    TelemetryStore store;
    store.set_sensors({{0, 1, 1.5, 10.0}, {0, 2, 2.5, 10.0}});
    store.set_sensors({{1, 0, 9.0, 20.0}});
    auto s = store.snapshot_sensors();
    BOOST_REQUIRE_EQUAL(s.size(), 1u);
    BOOST_TEST(s[0].hat_id == 1);
    BOOST_TEST(s[0].value == 9.0);
}

BOOST_AUTO_TEST_CASE(gpio_and_relay_state_round_trip) {
    TelemetryStore store;
    store.set_gpio_states({{17, 1}, {27, 0}});
    std::bitset<16> relays;
    relays.set(0);
    relays.set(15);
    store.set_relay_state(relays);
    BOOST_TEST(store.snapshot_gpio_states().at(17) == 1);
    BOOST_TEST(store.snapshot_gpio_states().at(27) == 0);
    BOOST_TEST((store.snapshot_relay_state() == relays));
}

BOOST_AUTO_TEST_CASE(servo_upsert_updates_in_place) {
    TelemetryStore store;
    store.upsert_servo_state(3, 90, true);
    store.upsert_servo_state(5, 10, false);
    store.upsert_servo_state(3, 120, false);
    auto s = store.snapshot_servo_states();
    BOOST_REQUIRE_EQUAL(s.size(), 2u);
    BOOST_TEST(s[0].id == 3);
    BOOST_TEST(s[0].angle == 120);
    BOOST_TEST(!s[0].enabled);
    BOOST_TEST(s[1].id == 5);
}

// Meaningful under ThreadSanitizer: concurrent writers and a reader.
BOOST_AUTO_TEST_CASE(concurrent_writers_and_reader) {
    TelemetryStore store;
    std::atomic<bool> stop{false};
    std::atomic<bool> oversized{false};
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t) {
        writers.emplace_back([&store, t] {
            for (int i = 0; i < 2000; ++i) {
                store.set_sensors({{t, i, double(i), double(i)}});
                store.upsert_servo_state(t, uint16_t(i % 180), true);
                store.set_relay_state(std::bitset<16>(uint64_t(i)));
                store.set_gpio_states({{t, i & 1}});
            }
        });
    }
    std::thread reader([&] {
        while (!stop) {
            if (store.snapshot_sensors().size() > 1u) oversized = true;
            store.snapshot_servo_states();
            store.snapshot_relay_state();
            store.snapshot_gpio_states();
        }
    });
    for (auto& th : writers) th.join();
    stop = true;
    reader.join();
    BOOST_TEST(!oversized.load());
    BOOST_TEST(store.snapshot_servo_states().size() == 4u);
}

BOOST_AUTO_TEST_SUITE_END()

#include "loops.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace json = boost::json;
using namespace std::chrono;

namespace {
const std::string kTelemetryTopic = "nova/telemetry";
}

void publisher_loop(mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms) {
    int interval_ms = publish_interval_ms > 0 ? publish_interval_ms : 1;
    while (true) {
        try {
            json::array json_sensor_data;
            json::array json_gpio_data;

            for (const auto& sd : telemetry.snapshot_sensors()) {
                json::object se;
                se["hat_id"] = sd.hat_id;
                se["channel_id"] = sd.channel_id;
                se["value"] = sd.value;
                se["timestamp"] = sd.timestamp_ms;
                json_sensor_data.push_back(se);
            }

            for (const auto& [pin, state] : telemetry.snapshot_gpio_states()) {
                json::object g;
                g["pin_id"] = pin;
                g["state"] = state;
                json_gpio_data.push_back(g);
            }

            json::object payload;
            payload["source"] = source_id;
            payload["sensors"] = json_sensor_data;
            payload["gpios"] = json_gpio_data;

            std::string s_payload = json::serialize(payload);

            if (cli && cli->is_connected()) {
                try {
                    cli->publish(kTelemetryTopic, s_payload)->wait();
                } catch (const std::exception& e) {
                    std::cerr << "Publish failed: " << e.what() << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Publisher error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Publisher error: unknown exception" << std::endl;
        }

        std::this_thread::sleep_for(milliseconds(interval_ms));
    }
}

void consumer_loop(mqtt::async_client_ptr cli, CommandRouter& router) {
    while (true) {
        try {
            auto msg = cli ? cli->consume_message() : nullptr;
            if (!msg) {
                std::this_thread::sleep_for(milliseconds(1));
                continue;
            }

            std::string m_str = msg->to_string();
            router.handle_message(m_str);
        } catch (const std::exception& e) {
            std::cerr << "Consumer error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Consumer error: unknown exception" << std::endl;
        }
    }
}

void gpio_sampler_loop(GPIO_Manager& manager, TelemetryStore& telemetry) {
    while (true) {
        try {
            auto input_vals = manager.read_all_inputs();
            telemetry.set_gpio_states(std::move(input_vals));
        } catch (const std::exception& e) {
            std::cerr << "GPIO sampler error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "GPIO sampler error: unknown exception" << std::endl;
        }
        std::this_thread::sleep_for(milliseconds(50));
    }
}

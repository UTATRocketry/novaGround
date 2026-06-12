#include "loops.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <iostream>
#include <map>
#include <string>
#include <thread>

namespace json = boost::json;
using namespace std::chrono;

namespace {
const std::string kTelemetryTopic    = "nova/telemetry";
constexpr double  kBoardTimeoutS     = 3.0;   // mirrors Python GS HEARTBEAT_TIMEOUT_S
constexpr int     kDiscoveryIntervalS = 2;
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

            // MCC DAQ + FAS ADC samples (FAS hat_id >= 100, mixed in transparently)
            for (const auto& sd : telemetry.snapshot_sensors()) {
                json::object se;
                se["hat_id"]    = sd.hat_id;
                se["channel_id"] = sd.channel_id;
                se["value"]     = sd.value;
                se["timestamp"] = sd.timestamp_ms;
                json_sensor_data.push_back(se);
            }

            for (const auto& [pin, state] : telemetry.snapshot_gpio_states()) {
                json::object g;
                g["pin_id"] = pin;
                g["state"]  = state;
                json_gpio_data.push_back(g);
            }

            // FAS board status
            json::array json_fas_boards;
            for (const auto& b : telemetry.snapshot_fas_boards()) {
                json::object bo;
                bo["key"]       = b.key;
                bo["online"]    = b.online;
                bo["uptime_ms"] = b.uptime_ms;
                json_fas_boards.push_back(bo);
            }

            // FAS IMC status
            FasImcStatus imc = telemetry.snapshot_fas_imc();
            json::object json_fas_imc;
            json_fas_imc["board_id"]    = imc.board_id;
            json_fas_imc["armed"]       = imc.armed;
            json_fas_imc["arm_line"]    = imc.arm_line;
            json_fas_imc["disarm_line"] = imc.disarm_line;

            json::object payload;
            payload["source"]     = source_id;
            payload["sensors"]    = json_sensor_data;
            payload["gpios"]      = json_gpio_data;
            payload["fas_boards"] = json_fas_boards;
            payload["fas_imc"]    = json_fas_imc;

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

void fas_discovery_loop(FasLink& link, TelemetryStore& telemetry) {
    // Track when each board was last heard from so we can mark it offline.
    std::map<std::string, steady_clock::time_point> last_seen;

    // Register a heartbeat callback to refresh the last-seen table.
    // This is set up here rather than in main so the loop owns the table.
    link.on_heartbeat([&last_seen, &telemetry](int board_id,
                                               const rt_heartbeat_t& hb) {
        // Build the board key from what we know (heartbeat doesn't carry kind,
        // but we update uptime for any already-known board keyed by board_id).
        // A full key requires the DISCOVERY_ANNOUNCE; heartbeat just refreshes.
        for (auto& [key, tp] : last_seen) {
            // The key ends with ":<board_id>"; check the suffix.
            auto colon = key.rfind(':');
            if (colon != std::string::npos &&
                std::stoi(key.substr(colon + 1)) == board_id) {
                tp = steady_clock::now();
                FasBoardStatus bs;
                bs.key       = key;
                bs.online    = true;
                bs.uptime_ms = hb.uptime_ms;
                telemetry.upsert_fas_board(bs);
            }
        }
    });

    while (true) {
        try {
            link.send_discovery_req();

            // Check for boards that haven't been heard from in kBoardTimeoutS.
            auto now = steady_clock::now();
            for (auto& [key, tp] : last_seen) {
                double elapsed =
                    duration_cast<duration<double>>(now - tp).count();
                if (elapsed > kBoardTimeoutS) {
                    FasBoardStatus bs;
                    bs.key    = key;
                    bs.online = false;
                    telemetry.upsert_fas_board(bs);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "fas_discovery_loop error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "fas_discovery_loop error: unknown exception" << std::endl;
        }

        std::this_thread::sleep_for(seconds(kDiscoveryIntervalS));
    }
}

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
const std::string kEngineTopic = "nova/telemetry/engine";
const std::string kFlightTopic = "nova/telemetry/flight";
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

            // FAS ADC samples — published as source="FAS" with node/channel keys
            // matching the config bindings (e.g. node="EPB_1", channel=0).
            // Node string is derived from the board key ("EPB:0" → "EPB_1").
            json::array json_fas_sensors;
            {
                // Collapse to latest sample per (board_id, channel).
                std::map<std::pair<int,int>, const FasAdcSample*> latest;
                auto adc_snap = telemetry.snapshot_fas_adc();
                for (const auto& s : adc_snap)
                    for (int ch = 0; ch < 2; ++ch)
                        latest[{s.board_id, ch}] = &s;

                // Build board_id → node_string map from the board status keys.
                std::map<int, std::string> board_node;
                for (const auto& b : telemetry.snapshot_fas_boards()) {
                    // key format: "EPB:0" → node "EPB_1" (1-based)
                    auto colon = b.key.find(':');
                    if (colon == std::string::npos) continue;
                    std::string kind = b.key.substr(0, colon);
                    int bid = std::stoi(b.key.substr(colon + 1));
                    board_node[bid] = kind + "_" + std::to_string(bid + 1);
                }

                double ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();

                for (const auto& [key, sp] : latest) {
                    auto node_it = board_node.find(key.first);
                    if (node_it == board_node.end()) continue;
                    json::object se;
                    se["node"]      = node_it->second;
                    se["channel"]   = key.second;
                    se["value"]     = (key.second == 0) ? sp->v[0] : sp->v[1];
                    se["timestamp"] = static_cast<int64_t>(ts);
                    json_fas_sensors.push_back(se);
                }
            }

            json::object engine_payload;
            engine_payload["source"]  = source_id;
            engine_payload["sensors"] = json_sensor_data;
            engine_payload["gpios"]   = json_gpio_data;

            json::object fas_sensor_payload;
            fas_sensor_payload["source"]  = "FAS";
            fas_sensor_payload["sensors"] = json_fas_sensors;

            json::object flight_payload;
            flight_payload["source"]     = source_id;
            flight_payload["fas_boards"] = json_fas_boards;
            flight_payload["fas_imc"]    = json_fas_imc;

            if (cli && cli->is_connected()) {
                try {
                    cli->publish(kEngineTopic, json::serialize(engine_payload))->wait();
                    if (!json_fas_sensors.empty())
                        cli->publish(kEngineTopic, json::serialize(fas_sensor_payload))->wait();
                    cli->publish(kFlightTopic, json::serialize(flight_payload))->wait();
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

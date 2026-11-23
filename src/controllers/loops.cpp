#include "loops.hpp"

#include <boost/json.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "utils/interruptible_sleep.hpp"

namespace json = boost::json;
using namespace std::chrono;

namespace {
const std::string kTelemetryTopic = "nova/telemetry/engine";
const int kConsumerPollMs = 100;
}

std::string stats_topic(const std::string& node_id) {
    return "nova/stats/" + node_id;
}

std::string stats_info_topic(const std::string& node_id) {
    return stats_topic(node_id) + "/info";
}

std::string state_topic(const std::string& node_id) {
    return "nova/state/" + node_id;
}

void publisher_loop(std::stop_token stop,
                    mqtt::async_client_ptr cli,
                    TelemetryStore& telemetry,
                    std::string source_id,
                    int publish_interval_ms,
                    StatsRegistry* stats) {
    int interval_ms = publish_interval_ms > 0 ? publish_interval_ms : 1;
    LoopStats* loop_stats = stats ? stats->loop("publisher", interval_ms) : nullptr;
    InterruptibleSleep sleeper;
    while (!stop.stop_requested()) {
        if (loop_stats) {
            loop_stats->tick();
        }
        try {
            json::array json_sensor_data;
            json::array json_gpio_data;
            double newest_sample_ms = 0.0;

            for (const auto& sd : telemetry.snapshot_sensors()) {
                newest_sample_ms = std::max(newest_sample_ms, sd.timestamp_ms);
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
            if (loop_stats) {
                payload["seq"] = stats->next_telemetry_seq();
                if (newest_sample_ms > 0.0) {
                    payload["sample_ms"] = newest_sample_ms;
                }
                payload["publish_ms"] =
                    duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            }

            std::string s_payload = json::serialize(payload);

            if (cli && cli->is_connected()) {
                try {
                    cli->publish(kTelemetryTopic, s_payload)->wait();
                } catch (const std::exception& e) {
                    std::cerr << "Publish failed: " << e.what() << std::endl;
                    if (stats) {
                        stats->count_publish_failure();
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Publisher error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Publisher error: unknown exception" << std::endl;
        }

        if (sleeper.sleep_for(stop, milliseconds(interval_ms))) {
            break;
        }
    }
}

void consumer_loop(std::stop_token stop, mqtt::async_client_ptr cli, CommandRouter& router) {
    InterruptibleSleep sleeper;
    while (!stop.stop_requested()) {
        try {
            if (!cli) {
                sleeper.sleep_for(stop, milliseconds(1));
                continue;
            }
            // Wake up regularly so a stop request is noticed even when no
            // message arrives.
            mqtt::const_message_ptr msg;
            if (!cli->try_consume_message_for(&msg, milliseconds(kConsumerPollMs)) || !msg) {
                continue;
            }

            const auto received = steady_clock::now();
            std::string m_str = msg->to_string();
            router.handle_message(m_str, received);
        } catch (const std::exception& e) {
            std::cerr << "Consumer error: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Consumer error: unknown exception" << std::endl;
        }
    }
}

void stats_loop(std::stop_token stop,
                mqtt::async_client_ptr cli,
                StatsRegistry& stats,
                std::string node_id,
                int interval_ms) {
    const std::string topic = stats_topic(node_id);
    InterruptibleSleep sleeper;
    // Sleep first: the window then covers a full interval.
    while (!sleeper.sleep_for(stop, milliseconds(interval_ms > 0 ? interval_ms : 1000))) {
        try {
            std::string message = stats.take_stats_json();
            if (cli && cli->is_connected()) {
                cli->publish(topic, message)->wait();
            }
        } catch (const std::exception& e) {
            std::cerr << "Stats publish failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Stats publish failed: unknown exception" << std::endl;
        }
    }
}

void state_loop(std::stop_token stop,
                mqtt::async_client_ptr cli,
                ActuatorSources sources,
                StatsRegistry& stats,
                std::string node_id,
                int interval_ms) {
    const std::string topic = state_topic(node_id);
    InterruptibleSleep sleeper;
    // Read first: the state the hardware started in is worth having too.
    do {
        try {
            std::string message = actuator_state_json(sources, &stats);
            if (cli && cli->is_connected()) {
                // Retained, so a subscriber that arrives later gets the last
                // state at once. `time_ms` in it says how old that is.
                cli->publish(topic, message, 1, true)->wait();
            }
        } catch (const std::exception& e) {
            std::cerr << "State publish failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "State publish failed: unknown exception" << std::endl;
        }
    } while (!sleeper.sleep_for(stop, milliseconds(interval_ms > 0 ? interval_ms : 1000)));
}

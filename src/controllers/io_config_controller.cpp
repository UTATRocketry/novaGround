#include "io_config_controller.hpp"

#include <boost/json.hpp>
#include <chrono>
#include <iostream>
#include <set>

#include "controllers/gpio_controller.hpp"
#include "core/data_logger.hpp"
#include "core/output_supervisor.hpp"

namespace json = boost::json;

IoConfigController::IoConfigController(GpioController* gpio,
                                       AnalogIo* daq,
                                       std::vector<DaqHatDevice> hats,
                                       OutputSupervisor* supervisor,
                                       DataLogger* logger,
                                       SystemMs system_ms)
    : gpio_(gpio),
      daq_(daq),
      hats_(std::move(hats)),
      supervisor_(supervisor),
      logger_(logger),
      system_ms_(system_ms ? std::move(system_ms) : SystemMs([] {
          using namespace std::chrono;
          return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
      })) {
    // AnalogIo::open set every MCC 134 channel to kUnconfiguredTcType.
    for (const auto& hat : hats_) {
        if (!hat.thermocouples) continue;
        for (int channel : hat.channels) {
            tc_types_[hat.address][channel] = kUnconfiguredTcType;
        }
    }
}

void IoConfigController::set_publisher(Publish publish) {
    std::lock_guard<std::mutex> lock(mutex_);
    publish_ = std::move(publish);
}

void IoConfigController::handle_message(const std::string& payload) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::set<int> mcc134;
        for (const auto& [address, channels] : tc_types_) mcc134.insert(address);

        ParsedIoConfig parsed = parse_pi_io_config(payload, mcc134);
        std::vector<ConfigRejection> rejected = parsed.rejected;
        if (!parsed.usable) {
            std::cerr << "Config on the config topic not applied: " << rejected.front().reason << std::endl;
            rejected_ = std::move(rejected);
        } else {
            const PiIoConfig& config = parsed.config;

            // GPIO: claim and release pins to match the config.
            std::set<int> outputs;
            for (const auto& [pin, limits] : config.gpio_outputs) outputs.insert(pin);
            std::set<int> claimed_outputs;
            if (gpio_) {
                for (auto& failure : gpio_->apply(outputs, config.gpio_inputs)) {
                    rejected.push_back(std::move(failure));
                }
                for (int pin : gpio_->output_pins()) claimed_outputs.insert(pin);
            }

            // Thermocouple types: every MCC 134 channel gets its configured
            // type, or kUnconfiguredTcType. Only changes are written.
            for (auto& [address, channels] : tc_types_) {
                for (auto& [channel, current] : channels) {
                    TcType wanted = kUnconfiguredTcType;
                    bool configured = false;
                    if (auto hat = config.daq.find(address); hat != config.daq.end()) {
                        if (auto ch = hat->second.find(channel); ch != hat->second.end()) {
                            wanted = ch->second;
                            configured = true;
                        }
                    }
                    if (wanted == current) continue;
                    if (daq_ && daq_->set_tc_type(address, channel, wanted)) {
                        current = wanted;
                    } else {
                        const std::string item = "daq." + std::to_string(address) + "." + std::to_string(channel);
                        rejected.push_back({item, std::string("setting type ") + tc_type_name(wanted) +
                                                      " failed; it stays " + tc_type_name(current) +
                                                      (configured ? "" : " (channel not in the config)")});
                    }
                }
            }

            // Limits: GPIO outputs that are claimed, and the relays listed.
            if (supervisor_) {
                std::map<OutputId, OutputLimits> limits;
                for (const auto& [pin, l] : config.gpio_outputs) {
                    if (claimed_outputs.count(pin)) limits[gpio_output(pin)] = l;
                }
                for (const auto& [channel, l] : config.relays) {
                    limits[relay_output(channel)] = l;
                }
                supervisor_->configure(std::move(limits), config.heartbeat_timeout_ms);
            }

            // The next recording's actuator columns.
            if (logger_) {
                std::vector<int> pins(claimed_outputs.begin(), claimed_outputs.end());
                logger_->set_actuator_columns(actuator_log_headers(pins), pins);
            }

            config_hash_ = config.config_hash;
            applied_ms_ = system_ms_();
            rejected_ = std::move(rejected);
            std::cout << "Config applied: hash " << config_hash_.value_or("(none)") << ", "
                      << claimed_outputs.size() << " GPIO output(s), " << config.gpio_inputs.size()
                      << " input(s) listed, " << config.relays.size() << " relay limit(s), heartbeat "
                      << (config.heartbeat_timeout_ms ? std::to_string(*config.heartbeat_timeout_ms) + " ms" : "off")
                      << ", " << rejected_.size() << " item(s) rejected" << std::endl;
        }
        for (const auto& r : rejected_) {
            std::cerr << "  rejected " << r.item << ": " << r.reason << std::endl;
        }
    }
    publish_status();
}

std::string IoConfigController::status_json() {
    json::object root;
    json::array rejected;
    json::object thermocouples;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        root["config_hash"] = config_hash_ ? json::value(*config_hash_) : json::value(nullptr);
        root["applied_ms"] = applied_ms_ ? json::value(*applied_ms_) : json::value(nullptr);
        for (const auto& r : rejected_) {
            rejected.push_back(json::object{{"item", r.item}, {"reason", r.reason}});
        }
        for (const auto& [address, channels] : tc_types_) {
            json::object hat;
            for (const auto& [channel, type] : channels) {
                hat[std::to_string(channel)] = tc_type_name(type);
            }
            thermocouples[std::to_string(address)] = std::move(hat);
        }
    }
    root["rejected"] = std::move(rejected);

    json::array reserved;
    for (const auto& r : reserved_pins()) reserved.push_back(r.pin);
    root["reserved_pins"] = std::move(reserved);
    root["thermocouples"] = std::move(thermocouples);

    json::array tripped;
    if (supervisor_) {
        for (const auto& trip : supervisor_->tripped()) {
            tripped.push_back(json::object{
                {"output", trip.output.name()}, {"reason", trip.reason}, {"time_ms", trip.time_ms}});
        }
    }
    root["tripped"] = std::move(tripped);
    return json::serialize(root);
}

void IoConfigController::publish_status() {
    Publish publish;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        publish = publish_;
    }
    if (!publish) {
        return;
    }
    try {
        publish(status_json());
    } catch (const std::exception& e) {
        std::cerr << "Config status publish failed: " << e.what() << std::endl;
    }
}

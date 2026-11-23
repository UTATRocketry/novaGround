#include "actuator_state.hpp"

#include <boost/json.hpp>
#include <exception>
#include <iostream>
#include <map>

namespace json = boost::json;

namespace {
constexpr int kSchema = 1;

json::object relays_json(RelayBank& relays, std::uint64_t& errors) {
    json::object o;
    std::bitset<16> state;
    bool ok = false;
    try {
        ok = relays.is_ready() && relays.read_output(state);
    } catch (const std::exception& e) {
        std::cerr << "Relay read-back failed: " << e.what() << std::endl;
    }
    o["ok"] = ok;
    if (ok) {
        // Bit i is relay i, the same numbering as the relay command's id.
        o["mask"] = static_cast<std::uint64_t>(state.to_ulong());
    } else {
        ++errors;
    }
    return o;
}

const char* output_name(PwmOutput output) {
    switch (output) {
        case PwmOutput::Off:
            return "off";
        case PwmOutput::On:
            return "on";
        case PwmOutput::Pulse:
            return "pulse";
    }
    return "off";
}

json::object servos_json(PwmDriver& servo, std::uint64_t& errors) {
    json::object o;
    json::array channels;
    bool all_ok = servo.is_ready();
    if (!all_ok) {
        ++errors;
    }
    for (int id = 0; id < kServoChannels && servo.is_ready(); ++id) {
        json::object c;
        c["id"] = id;
        PwmChannelReadback readback;
        bool ok = false;
        try {
            ok = servo.read_channel(static_cast<std::uint8_t>(id), readback);
        } catch (const std::exception& e) {
            std::cerr << "Servo read-back failed: " << e.what() << std::endl;
        }
        if (ok) {
            c["output"] = output_name(readback.output);
            if (readback.output == PwmOutput::Pulse) {
                c["pulse_us"] = readback.microseconds;
            }
        } else {
            c["output"] = "unreadable";
            all_ok = false;
            ++errors;
        }
        channels.push_back(std::move(c));
    }
    o["ok"] = all_ok;
    o["channels"] = std::move(channels);
    return o;
}

json::object gpios_json(DigitalIo& gpio, std::uint64_t& errors) {
    json::object o;
    json::array outputs;
    bool ok = true;
    try {
        for (const auto& [pin, level] : gpio.read_all_outputs()) {
            json::object g;
            g["pin_id"] = pin;
            g["state"] = level;
            outputs.push_back(std::move(g));
        }
    } catch (const std::exception& e) {
        std::cerr << "GPIO read-back failed: " << e.what() << std::endl;
        ok = false;
        outputs.clear();
        ++errors;
    }
    o["ok"] = ok;
    o["outputs"] = std::move(outputs);
    return o;
}
}  // namespace

std::string actuator_state_json(const ActuatorSources& sources,
                                StatsRegistry* stats,
                                std::chrono::system_clock::time_point now) {
    json::object root;
    root["schema"] = kSchema;
    root["time_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::uint64_t errors = 0;
    if (sources.relays) {
        root["relays"] = relays_json(*sources.relays, errors);
    }
    if (sources.servo) {
        root["servos"] = servos_json(*sources.servo, errors);
    }
    if (sources.gpio) {
        root["gpios"] = gpios_json(*sources.gpio, errors);
    }
    if (stats) {
        stats->count_actuator_read_errors(errors);
    }
    return json::serialize(root);
}

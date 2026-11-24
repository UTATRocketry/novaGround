#include "pi_io_config.hpp"

#include <boost/json.hpp>
#include <cctype>
#include <exception>

namespace json = boost::json;

namespace {
// Sources: docs/design/pi-io-config.md, "Reserved pins".
const std::vector<ReservedPin> kReserved = {
    {0, "HAT ID EEPROM (ID_SD)"},
    {1, "HAT ID EEPROM (ID_SC)"},
    {2, "I2C1 SDA (relay board, servo driver)"},
    {3, "I2C1 SCL (relay board, servo driver)"},
    {7, "SPI0 CE1"},
    {8, "SPI0 CE0"},
    {9, "SPI0 MISO"},
    {10, "SPI0 MOSI"},
    {11, "SPI0 SCLK"},
    {12, "DAQ HAT address select (ADDR0)"},
    {13, "DAQ HAT address select (ADDR1)"},
    {16, "MCC 128 reset"},
    {20, "MCC 128 interrupt"},
    {21, "daqhats shared interrupt"},
    {26, "DAQ HAT address select (ADDR2)"},
};

void reject(ParsedIoConfig& out, std::string item, std::string reason) {
    out.rejected.push_back({std::move(item), std::move(reason)});
}

// A non-negative decimal number with no sign, spaces or leading zeros ("0",
// "17"); nullopt for anything else. A key like "017" would otherwise name the
// same pin as "17" without looking like it.
std::optional<int> parse_index(const std::string& text) {
    if (text.empty() || text.size() > 4) return std::nullopt;
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    if (text.size() > 1 && text[0] == '0') return std::nullopt;
    return std::stoi(text);
}

// A whole JSON number (no fraction, no bool). nullopt otherwise.
std::optional<std::int64_t> whole_number(const json::value& v) {
    if (v.is_int64()) return v.as_int64();
    if (v.is_uint64()) {
        if (v.as_uint64() > static_cast<std::uint64_t>(INT64_MAX)) return std::nullopt;
        return static_cast<std::int64_t>(v.as_uint64());
    }
    return std::nullopt;
}

// Why a GPIO pin can't be used, or empty if it can.
std::string pin_problem(int pin) {
    if (pin < 0 || pin > kMaxBcmPin) {
        return "outside BCM 0-" + std::to_string(kMaxBcmPin);
    }
    if (const char* use = reserved_pin_use(pin)) {
        return std::string("reserved (") + use + ")";
    }
    return {};
}

// One output's limits: { "max_on_ms": <positive whole number> | null,
// "on_comms_loss": "hold" | "safe" }, both optional. Empty string if fine,
// otherwise why not.
std::string parse_limits(const json::value& v, OutputLimits& limits) {
    if (!v.is_object()) {
        return "not an object";
    }
    for (const auto& [key, value] : v.as_object()) {
        const std::string k(key);
        if (k == "max_on_ms") {
            if (value.is_null()) {
                limits.max_on_ms.reset();
                continue;
            }
            auto ms = whole_number(value);
            if (!ms || *ms <= 0) {
                return "max_on_ms must be a positive whole number of milliseconds or null";
            }
            limits.max_on_ms = *ms;
        } else if (k == "on_comms_loss") {
            if (!value.is_string()) {
                return "on_comms_loss must be \"hold\" or \"safe\"";
            }
            const std::string text(value.as_string());
            if (text == "safe") {
                limits.safe_on_comms_loss = true;
            } else if (text == "hold") {
                limits.safe_on_comms_loss = false;
            } else {
                return "on_comms_loss must be \"hold\" or \"safe\"";
            }
        } else {
            return "unknown key '" + k + "'";
        }
    }
    return {};
}

void parse_gpio(const json::value& v, ParsedIoConfig& out) {
    if (!v.is_object()) {
        reject(out, "gpio", "not an object");
        return;
    }
    std::map<int, OutputLimits> outputs;
    std::set<int> inputs;
    for (const auto& [key, value] : v.as_object()) {
        const std::string k(key);
        if (k == "outputs") {
            if (!value.is_object()) {
                reject(out, "gpio.outputs", "not an object");
                continue;
            }
            for (const auto& [pin_key, entry] : value.as_object()) {
                const std::string item = "gpio.outputs." + std::string(pin_key);
                auto pin = parse_index(std::string(pin_key));
                if (!pin) {
                    reject(out, item, "not a pin number");
                    continue;
                }
                if (auto problem = pin_problem(*pin); !problem.empty()) {
                    reject(out, item, problem);
                    continue;
                }
                OutputLimits limits;
                if (auto problem = parse_limits(entry, limits); !problem.empty()) {
                    reject(out, item, problem);
                    continue;
                }
                outputs[*pin] = limits;
            }
        } else if (k == "inputs") {
            if (!value.is_array()) {
                reject(out, "gpio.inputs", "not an array");
                continue;
            }
            std::size_t index = 0;
            for (const auto& entry : value.as_array()) {
                auto pin = whole_number(entry);
                if (!pin) {
                    reject(out, "gpio.inputs[" + std::to_string(index) + "]", "not a pin number");
                } else {
                    const std::string item = "gpio.inputs." + std::to_string(*pin);
                    const std::string problem = (*pin < 0 || *pin > kMaxBcmPin)
                                                    ? pin_problem(-1)
                                                    : pin_problem(static_cast<int>(*pin));
                    if (!problem.empty()) {
                        reject(out, item, problem);
                    } else if (inputs.count(static_cast<int>(*pin))) {
                        reject(out, item, "listed twice");
                    } else {
                        inputs.insert(static_cast<int>(*pin));
                    }
                }
                ++index;
            }
        } else {
            reject(out, "gpio." + k, "unknown key");
        }
    }

    // A pin that is both is neither: claiming it either way could be wrong.
    for (auto it = outputs.begin(); it != outputs.end();) {
        if (inputs.count(it->first)) {
            const std::string pin = std::to_string(it->first);
            reject(out, "gpio.outputs." + pin, "listed as both input and output");
            reject(out, "gpio.inputs." + pin, "listed as both input and output");
            inputs.erase(it->first);
            it = outputs.erase(it);
        } else {
            ++it;
        }
    }
    out.config.gpio_outputs = std::move(outputs);
    out.config.gpio_inputs = std::move(inputs);
}

void parse_relays(const json::value& v, ParsedIoConfig& out) {
    if (!v.is_object()) {
        reject(out, "relays", "not an object");
        return;
    }
    for (const auto& [key, entry] : v.as_object()) {
        const std::string item = "relays." + std::string(key);
        auto channel = parse_index(std::string(key));
        if (!channel || *channel >= kRelayChannels) {
            reject(out, item, "not a relay channel (0-" + std::to_string(kRelayChannels - 1) + ")");
            continue;
        }
        OutputLimits limits;
        if (auto problem = parse_limits(entry, limits); !problem.empty()) {
            reject(out, item, problem);
            continue;
        }
        out.config.relays[*channel] = limits;
    }
}

void parse_daq(const json::value& v, const std::set<int>& mcc134_addresses, ParsedIoConfig& out) {
    if (!v.is_object()) {
        reject(out, "daq", "not an object");
        return;
    }
    for (const auto& [address_key, channels] : v.as_object()) {
        const std::string hat_item = "daq." + std::string(address_key);
        auto address = parse_index(std::string(address_key));
        if (!address) {
            reject(out, hat_item, "not a HAT address");
            continue;
        }
        if (!mcc134_addresses.count(*address)) {
            reject(out, hat_item, "no MCC 134 at this address");
            continue;
        }
        if (!channels.is_object()) {
            reject(out, hat_item, "not an object");
            continue;
        }
        for (const auto& [channel_key, type] : channels.as_object()) {
            const std::string item = hat_item + "." + std::string(channel_key);
            auto channel = parse_index(std::string(channel_key));
            if (!channel || *channel > 3) {
                reject(out, item, "not an MCC 134 channel (0-3)");
                continue;
            }
            std::optional<TcType> parsed;
            if (type.is_string()) {
                parsed = parse_tc_type(std::string(type.as_string()));
            }
            if (!parsed) {
                reject(out, item, "unknown thermocouple type (J, K, T, E, R, S, B or N)");
                continue;
            }
            out.config.daq[*address][*channel] = *parsed;
        }
    }
}

void parse_heartbeat(const json::value& v, ParsedIoConfig& out) {
    if (!v.is_object()) {
        reject(out, "heartbeat", "not an object");
        return;
    }
    std::optional<std::int64_t> timeout;
    bool ok = true;
    for (const auto& [key, value] : v.as_object()) {
        const std::string k(key);
        if (k == "timeout_ms") {
            auto ms = whole_number(value);
            if (!ms || *ms <= 0) {
                reject(out, "heartbeat.timeout_ms", "must be a positive whole number of milliseconds");
                ok = false;
            } else {
                timeout = *ms;
            }
        } else {
            reject(out, "heartbeat." + k, "unknown key");
        }
    }
    if (ok && !timeout) {
        reject(out, "heartbeat.timeout_ms", "missing");
        ok = false;
    }
    if (ok) {
        out.config.heartbeat_timeout_ms = timeout;
    }
}
}  // namespace

const std::vector<ReservedPin>& reserved_pins() {
    return kReserved;
}

const char* reserved_pin_use(int pin) {
    for (const auto& r : kReserved) {
        if (r.pin == pin) return r.use;
    }
    return nullptr;
}

ParsedIoConfig parse_pi_io_config(const std::string& payload, const std::set<int>& mcc134_addresses) {
    ParsedIoConfig out;
    boost::system::error_code ec;
    json::value root = json::parse(payload, ec);
    if (ec) {
        reject(out, "(message)", "not JSON: " + ec.message());
        return out;
    }
    if (!root.is_object()) {
        reject(out, "(message)", "not a JSON object");
        return out;
    }
    out.usable = true;
    try {
        for (const auto& [key, value] : root.as_object()) {
            const std::string k(key);
            if (k == "config_hash") {
                if (value.is_string()) {
                    out.config.config_hash = std::string(value.as_string());
                } else if (!value.is_null()) {
                    reject(out, "config_hash", "not a string");
                }
            } else if (k == "gpio") {
                parse_gpio(value, out);
            } else if (k == "relays") {
                parse_relays(value, out);
            } else if (k == "daq") {
                parse_daq(value, mcc134_addresses, out);
            } else if (k == "heartbeat") {
                parse_heartbeat(value, out);
            } else {
                reject(out, k, "unknown key");
            }
        }
    } catch (const std::exception& e) {
        // Nothing above should throw; if it does, apply nothing.
        out = ParsedIoConfig{};
        reject(out, "(message)", std::string("could not be read: ") + e.what());
    }
    return out;
}

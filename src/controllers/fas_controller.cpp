#include "fas_controller.hpp"

#include <cctype>
#include <iostream>
#include <string>

namespace json = boost::json;

namespace {

template <typename T>
T get_int(const json::object& obj, const char* key, T default_val = T{}) {
    if (auto* v = obj.if_contains(key)) {
        if (v->is_int64())  return static_cast<T>(v->as_int64());
        if (v->is_uint64()) return static_cast<T>(v->as_uint64());
        if (v->is_double()) return static_cast<T>(v->as_double());
    }
    return default_val;
}

bool get_bool(const json::object& obj, const char* key, bool default_val = false) {
    if (auto* v = obj.if_contains(key)) {
        if (v->is_bool())  return v->as_bool();
        if (v->is_int64()) return v->as_int64() != 0;
    }
    return default_val;
}

std::string get_str(const json::object& obj, const char* key) {
    if (auto* v = obj.if_contains(key); v && v->is_string())
        return std::string{v->as_string()};
    return {};
}

// Extract board_id from either the explicit "board_id" field or the legacy
// "node" string ("EPB_1" → 0, "EPB_2" → 1, …).
uint8_t resolve_board_id(const json::object& cmd) {
    if (cmd.contains("board_id"))
        return get_int<uint8_t>(cmd, "board_id");
    std::string node = get_str(cmd, "node");
    if (!node.empty()) {
        auto pos = node.rfind('_');
        if (pos != std::string::npos) {
            try {
                int n = std::stoi(node.substr(pos + 1));
                return static_cast<uint8_t>(std::max(0, n - 1));
            } catch (...) {}
        }
    }
    return 0;
}

// Convert pulse_us → Q15 duty (with a 20 ms default period).
uint16_t pulse_to_q15(uint32_t pulse_us, uint32_t period_us = 20000) {
    uint32_t p = period_us > 0 ? period_us : 20000;
    return static_cast<uint16_t>((static_cast<uint64_t>(pulse_us) * 32767) / p);
}

} // namespace

FasController::FasController(FasLink* link) : link_(link) {}

void FasController::handle_command(const json::object& cmd) {
    if (!link_) return;

    // Detect shape: node/port/action (novaOps high-level) vs. op (direct).
    if (cmd.contains("port")) {
        handle_node_port_command(cmd);
        return;
    }

    auto* op_val = cmd.if_contains("op");
    if (!op_val || !op_val->is_string()) {
        std::cerr << "[fas_controller] command missing \"op\" or \"port\"\n";
        return;
    }
    std::string op{op_val->as_string()};

    if (op == "pwm_set") {
        uint8_t  board_id  = get_int<uint8_t>(cmd, "board_id");
        uint8_t  channel   = get_int<uint8_t>(cmd, "channel");
        uint32_t pulse_us  = get_int<uint32_t>(cmd, "pulse_us");
        uint32_t period_us = get_int<uint32_t>(cmd, "period_us", 20000);
        link_->send_pwm_set(board_id, channel,
                            pulse_to_q15(pulse_us, period_us),
                            static_cast<uint16_t>(period_us));

    } else if (op == "load_sw_set") {
        uint8_t  board_id = get_int<uint8_t>(cmd, "board_id");
        uint8_t  channel  = get_int<uint8_t>(cmd, "channel");
        bool     enable   = get_bool(cmd, "enable");
        uint16_t hold_ms  = get_int<uint16_t>(cmd, "hold_ms", 0);
        link_->send_load_sw_set(board_id, channel, enable, hold_ms);

    } else if (op == "failsafe") {
        link_->send_actuator_failsafe(get_int<uint8_t>(cmd, "board_id"));

    } else if (op == "imc_arm") {
        link_->send_imc_arm(get_int<uint8_t>(cmd, "board_id"),
                            get_int<uint16_t>(cmd, "pulse_ms", 0));

    } else if (op == "imc_disarm") {
        link_->send_imc_disarm(get_int<uint8_t>(cmd, "board_id"),
                               get_int<uint16_t>(cmd, "pulse_ms", 0));

    } else if (op == "discover") {
        link_->send_discovery_req();

    } else if (op == "actuator_query") {
        link_->send_actuator_query(get_int<uint8_t>(cmd, "board_id"),
                                   get_int<uint8_t>(cmd, "channel"));

    } else {
        std::cerr << "[fas_controller] unknown op: " << op << "\n";
    }
}

void FasController::handle_node_port_command(const json::object& cmd) {
    uint8_t     board_id = resolve_board_id(cmd);
    uint8_t     channel  = get_int<uint8_t>(cmd, "channel");
    std::string port     = get_str(cmd, "port");
    std::string action   = get_str(cmd, "action");

    if (port == "relay") {
        bool enable  = (action == "on");
        uint16_t hold_ms = get_int<uint16_t>(cmd, "hold_ms", 0);
        link_->send_load_sw_set(board_id, channel, enable, hold_ms);

    } else if (port == "servo") {
        // "enable" → no-op.
        if (action == "enable") return;

        // "disable" or value=0 → park (PWM = 0 µs).
        // Any other action with a value field → positional move.
        uint32_t pulse_us = get_int<uint32_t>(cmd, "value", 0);
        if (action == "disable") pulse_us = 0;
        uint32_t period_us = get_int<uint32_t>(cmd, "period_us", 20000);
        link_->send_pwm_set(board_id, channel,
                            pulse_to_q15(pulse_us, period_us),
                            static_cast<uint16_t>(period_us));

    } else if (port == "gpio") {
        // Normalise action to upper-case for case-insensitive compare.
        std::string act_upper = action;
        for (auto& c : act_upper) c = static_cast<char>(std::toupper(c));
        if (act_upper == "ARM")
            link_->send_imc_arm(board_id, 0);
        else if (act_upper == "DISARM")
            link_->send_imc_disarm(board_id, 0);
        else
            std::cerr << "[fas_controller] unknown gpio action: " << action << "\n";

    } else {
        std::cerr << "[fas_controller] unknown port: " << port << "\n";
    }
}

#include "uart_controller.hpp"

#include <iostream>
#include <vector>

#include "utils/json_utils.hpp"

namespace {
constexpr size_t kUartCmdFixedPayloadSize = 8;
constexpr size_t kUartCmdMaxArgs = kUartFrameMaxPayload - kUartCmdFixedPayloadSize;

bool get_u8_field(const boost::json::object& cmd, const char* key, uint8_t& out) {
    auto value = get_int(cmd, key);
    if (!value || *value < 0 || *value > 0xFF) {
        return false;
    }
    out = static_cast<uint8_t>(*value);
    return true;
}

bool get_u32_field(const boost::json::object& cmd, const char* key, uint32_t& out) {
    auto value = get_int(cmd, key);
    if (!value || *value < 0 || *value > 0xFFFFFFFFLL) {
        return false;
    }
    out = static_cast<uint32_t>(*value);
    return true;
}

void put_u32_le(uint8_t* dst, uint32_t value) {
    dst[0] = static_cast<uint8_t>(value & 0xFF);
    dst[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

bool read_args(const boost::json::object& cmd, std::vector<uint8_t>& args) {
    auto args_it = cmd.if_contains("args");
    if (!args_it) {
        return true;
    }
    if (!args_it->is_array()) {
        std::cerr << "UART command args must be an array." << std::endl;
        return false;
    }

    const boost::json::array& arr = args_it->as_array();
    if (arr.size() > kUartCmdMaxArgs) {
        std::cerr << "UART command args exceed max length: " << arr.size() << std::endl;
        return false;
    }

    args.reserve(arr.size());
    for (const auto& value : arr) {
        int64_t parsed = -1;
        if (value.is_int64()) {
            parsed = value.as_int64();
        } else if (value.is_uint64() && value.as_uint64() <= 0xFF) {
            parsed = static_cast<int64_t>(value.as_uint64());
        } else if (value.is_double()) {
            parsed = static_cast<int64_t>(value.as_double());
        } else {
            std::cerr << "UART command arg is not numeric." << std::endl;
            return false;
        }

        if (parsed < 0 || parsed > 0xFF) {
            std::cerr << "UART command arg out of byte range: " << parsed << std::endl;
            return false;
        }
        args.push_back(static_cast<uint8_t>(parsed));
    }

    return true;
}
}

UartController::UartController(UartLink* link, uint8_t source_node)
    : link_(link), source_node_(source_node) {}

bool UartController::available() const {
    return link_ && link_->is_open();
}

uint8_t UartController::next_seq() {
    std::lock_guard<std::mutex> lock(mutex_);
    return ++seq_;
}

void UartController::handle_command(const boost::json::object& cmd) {
    if (!available()) {
        std::cerr << "UART controller unavailable." << std::endl;
        return;
    }

    uint8_t target = UART_NODE_UNKNOWN;
    uint32_t cmd_id = 0;
    uint8_t opcode = 0;
    if (!get_u8_field(cmd, "target", target) ||
        !get_u32_field(cmd, "cmd_id", cmd_id) ||
        !get_u8_field(cmd, "opcode", opcode)) {
        std::cerr << "UART command requires target, cmd_id, and opcode." << std::endl;
        return;
    }

    std::vector<uint8_t> args;
    if (!read_args(cmd, args)) {
        return;
    }

    uint8_t seq = next_seq();
    if (cmd.if_contains("seq")) {
        if (!get_u8_field(cmd, "seq", seq)) {
            std::cerr << "UART command seq must be a byte." << std::endl;
            return;
        }
    }

    uint8_t flags = 0;
    if (cmd.if_contains("flags")) {
        if (!get_u8_field(cmd, "flags", flags)) {
            std::cerr << "UART command flags must be a byte." << std::endl;
            return;
        }
    }

    UartFrame frame;
    frame.ver = 1;
    frame.msg = UART_MSG_CMD;
    frame.seq = seq;
    frame.flags = flags;
    frame.src = source_node_;
    frame.len = static_cast<uint8_t>(kUartCmdFixedPayloadSize + args.size());
    frame.payload[0] = source_node_;
    frame.payload[1] = target;
    put_u32_le(&frame.payload[2], cmd_id);
    frame.payload[6] = opcode;
    frame.payload[7] = static_cast<uint8_t>(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        frame.payload[kUartCmdFixedPayloadSize + i] = args[i];
    }

    if (!link_->send_frame(frame)) {
        std::cerr << "UART command send failed." << std::endl;
    }
}

#include "uart_frame.hpp"

#include <algorithm>

namespace {
uint16_t crc16_update(uint16_t crc, uint8_t byte) {
    crc ^= static_cast<uint16_t>(byte) << 8;
    for (int i = 0; i < 8; ++i) {
        crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                             : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}
}

UartFrameParser::UartFrameParser() {
    reset();
}

void UartFrameParser::reset() {
    state_ = State::Sof1;
    header_index_ = 0;
    payload_index_ = 0;
    current_ = {};
    crc_calc_ = 0xFFFF;
    crc_lo_ = 0;
}

bool UartFrameParser::feed(uint8_t byte, UartFrame& out) {
    switch (state_) {
    case State::Sof1:
        if (byte == kUartSof1) {
            state_ = State::Sof2;
        }
        break;

    case State::Sof2:
        if (byte == kUartSof2) {
            state_ = State::Header;
            header_index_ = 0;
            crc_calc_ = 0xFFFF;
        } else {
            state_ = State::Sof1;
        }
        break;

    case State::Header:
        header_[header_index_++] = byte;
        crc_calc_ = crc16_update(crc_calc_, byte);
        if (header_index_ == kUartHeaderSize) {
            current_.ver = header_[0];
            current_.msg = header_[1];
            current_.len = header_[2];
            current_.seq = header_[3];
            current_.flags = header_[4];
            current_.src = header_[5];

            if (current_.len > kUartFrameMaxPayload) {
                reset();
                break;
            }

            payload_index_ = 0;
            state_ = current_.len == 0 ? State::Crc1 : State::Payload;
        }
        break;

    case State::Payload:
        current_.payload[payload_index_++] = byte;
        crc_calc_ = crc16_update(crc_calc_, byte);
        if (payload_index_ == current_.len) {
            state_ = State::Crc1;
        }
        break;

    case State::Crc1:
        crc_lo_ = byte;
        state_ = State::Crc2;
        break;

    case State::Crc2: {
        uint16_t crc_rx = static_cast<uint16_t>(crc_lo_) | (static_cast<uint16_t>(byte) << 8);
        if (crc_rx == crc_calc_) {
            current_.crc = crc_rx;
            out = current_;
            reset();
            return true;
        }
        reset();
        break;
    }
    }

    return false;
}

uint16_t uart_crc16_ccitt_false(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}

std::vector<uint8_t> build_uart_frame(const UartFrame& frame) {
    uint8_t len = static_cast<uint8_t>(std::min<size_t>(frame.len, kUartFrameMaxPayload));
    std::vector<uint8_t> out(2 + kUartHeaderSize + len + 2);
    out[0] = kUartSof1;
    out[1] = kUartSof2;
    out[2] = frame.ver;
    out[3] = frame.msg;
    out[4] = len;
    out[5] = frame.seq;
    out[6] = frame.flags;
    out[7] = frame.src;
    std::copy_n(frame.payload.begin(), len, out.begin() + 8);

    uint16_t crc = uart_crc16_ccitt_false(out.data() + 2, kUartHeaderSize + len);
    out[8 + len] = static_cast<uint8_t>(crc & 0xFF);
    out[9 + len] = static_cast<uint8_t>((crc >> 8) & 0xFF);
    return out;
}

bool uart_frame_basic_valid(const UartFrame& frame) {
    if (frame.ver != 1 || frame.len > kUartFrameMaxPayload) {
        return false;
    }

    switch (frame.src) {
    case UART_NODE_EGSE:
    case UART_NODE_FMC:
    case UART_NODE_PMB:
    case UART_NODE_EPB1:
    case UART_NODE_EPB2:
    case UART_NODE_EPB3:
    case UART_NODE_EPB4:
    case UART_NODE_RAB:
    case UART_NODE_RADIO:
        return true;
    default:
        return false;
    }
}

const char* uart_msg_type_name(uint8_t msg) {
    switch (msg) {
    case UART_MSG_CMD:
        return "cmd";
    case UART_MSG_CFG:
        return "cfg";
    case UART_MSG_PING:
        return "ping";
    case UART_MSG_TIME:
        return "time";
    case UART_MSG_ACK:
        return "ack";
    case UART_MSG_TELEM:
        return "telem";
    case UART_MSG_EVENT:
        return "event";
    case UART_MSG_ERR:
        return "err";
    default:
        return "unknown";
    }
}

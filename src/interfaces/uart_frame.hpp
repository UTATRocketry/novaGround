#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

constexpr uint8_t kUartSof1 = 0xAA;
constexpr uint8_t kUartSof2 = 0x55;
constexpr size_t kUartFrameMaxPayload = 128;
constexpr size_t kUartHeaderSize = 6;
constexpr size_t kUartMaxWireSize = 2 + kUartHeaderSize + kUartFrameMaxPayload + 2;

enum UartMsgType : uint8_t {
    UART_MSG_CMD = 0x10,
    UART_MSG_CFG = 0x11,
    UART_MSG_PING = 0x12,
    UART_MSG_TIME = 0x13,
    UART_MSG_ACK = 0x20,
    UART_MSG_TELEM = 0x21,
    UART_MSG_EVENT = 0x22,
    UART_MSG_ERR = 0x23,
};

enum UartNetNode : uint8_t {
    UART_NODE_UNKNOWN = 0,
    UART_NODE_EGSE = 1,
    UART_NODE_FMC = 2,
    UART_NODE_PMB = 3,
    UART_NODE_EPB1 = 4,
    UART_NODE_EPB2 = 5,
    UART_NODE_EPB3 = 6,
    UART_NODE_EPB4 = 7,
    UART_NODE_RAB = 8,
    UART_NODE_RADIO = 9,
};

struct UartFrame {
    uint8_t ver = 1;
    uint8_t msg = 0;
    uint8_t len = 0;
    uint8_t seq = 0;
    uint8_t flags = 0;
    uint8_t src = UART_NODE_UNKNOWN;
    std::array<uint8_t, kUartFrameMaxPayload> payload{};
    uint16_t crc = 0;
};

class UartFrameParser {
public:
    UartFrameParser();

    void reset();
    bool feed(uint8_t byte, UartFrame& out);

private:
    enum class State {
        Sof1,
        Sof2,
        Header,
        Payload,
        Crc1,
        Crc2,
    };

    State state_ = State::Sof1;
    std::array<uint8_t, kUartHeaderSize> header_{};
    uint8_t header_index_ = 0;
    uint8_t payload_index_ = 0;
    UartFrame current_{};
    uint16_t crc_calc_ = 0xFFFF;
    uint8_t crc_lo_ = 0;
};

uint16_t uart_crc16_ccitt_false(const uint8_t* data, size_t len);
std::vector<uint8_t> build_uart_frame(const UartFrame& frame);
bool uart_frame_basic_valid(const UartFrame& frame);
const char* uart_msg_type_name(uint8_t msg);

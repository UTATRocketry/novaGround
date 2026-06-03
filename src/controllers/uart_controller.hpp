#pragma once

#include <boost/json.hpp>
#include <cstdint>
#include <mutex>

#include "interfaces/uart_link.hpp"

class UartController {
public:
    explicit UartController(UartLink* link, uint8_t source_node = UART_NODE_EGSE);

    void handle_command(const boost::json::object& cmd);

private:
    bool available() const;
    uint8_t next_seq();

    UartLink* link_ = nullptr;
    uint8_t source_node_ = UART_NODE_EGSE;
    uint8_t seq_ = 0;
    std::mutex mutex_;
};

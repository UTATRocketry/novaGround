#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>

#include "interfaces/uart_frame.hpp"

class UartLink {
public:
    UartLink(std::string device, int baud_rate);
    ~UartLink();

    UartLink(const UartLink&) = delete;
    UartLink& operator=(const UartLink&) = delete;

    bool open();
    void close();
    bool is_open() const;

    bool send_frame(const UartFrame& frame);
    std::optional<UartFrame> read_frame(std::chrono::milliseconds timeout);

    const std::string& device() const;
    int baud_rate() const;

private:
    bool configure_port();

    std::string device_;
    int baud_rate_ = 115200;
    int fd_ = -1;
    UartFrameParser parser_;
    std::deque<UartFrame> pending_frames_;
    mutable std::mutex io_mutex_;
};

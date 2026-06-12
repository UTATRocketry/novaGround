#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// RS-422 serial driver for the FAS FMC link.
//
// Wire format (from rt_proto.h RT_RS422_* defines):
//   [0xAA] [LEN_LO] [LEN_HI] [CAN_ID 4B LE] [data 0-8B] [CRC16_LO] [CRC16_HI]
// CRC-16/CCITT-FALSE covers LEN_LO, LEN_HI, and the full payload.
//
// Call open(), set_frame_callback(), then run() on a dedicated thread.
// send_frame() is thread-safe and may be called from any thread while run() is active.

class FasSerial {
public:
    using FrameCallback = std::function<void(uint32_t can_id,
                                             const uint8_t* data, size_t len)>;

    explicit FasSerial(std::string port, int baud = 460800);
    ~FasSerial();

    bool open();
    void close();
    bool is_open() const;

    void set_frame_callback(FrameCallback cb);

    // Blocks until stop() is called. Call on a dedicated std::thread.
    void run();
    void stop();

    // Encode and write one frame. Thread-safe. Returns false if the port is closed.
    bool send_frame(uint32_t can_id, const uint8_t* data, size_t len);

    struct Stats {
        int frames_decoded = 0;
        int frames_dropped = 0;
    };
    Stats stats() const;

private:
    static uint16_t crc16(const uint8_t* data, size_t len);

    // Frame parser state machine — mirrors protocol.py FrameParser.
    enum class State {
        WAIT_MAGIC,
        WAIT_LEN_LO,
        WAIT_LEN_HI,
        READ_PAYLOAD,
        WAIT_CRC_LO,
        WAIT_CRC_HI,
    };

    void step(uint8_t byte);
    void reset_parser();

    std::string port_;
    int baud_;
    int fd_ = -1;
    std::atomic<bool> running_{false};

    mutable std::mutex write_mutex_;
    FrameCallback frame_cb_;

    // Parser state
    State state_ = State::WAIT_MAGIC;
    uint16_t expected_len_ = 0;
    std::vector<uint8_t> buf_;
    uint8_t crc_lo_ = 0;

    int frames_decoded_ = 0;
    int frames_dropped_ = 0;
};

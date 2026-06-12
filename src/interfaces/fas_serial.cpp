#include "fas_serial.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>

// POSIX serial
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

static constexpr uint8_t kMagic      = 0xAA;
static constexpr uint16_t kMaxPayload = 4 + 8; // CAN_ID(4) + data(0..8)

FasSerial::FasSerial(std::string port, int baud)
    : port_(std::move(port)), baud_(baud) {}

FasSerial::~FasSerial() {
    stop();
    close();
}

bool FasSerial::open() {
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ < 0) {
        std::cerr << "[fas_serial] open " << port_ << ": " << std::strerror(errno) << "\n";
        return false;
    }

    // Switch to blocking reads.
    ::fcntl(fd_, F_SETFL, 0);

    struct termios tty {};
    if (::tcgetattr(fd_, &tty) != 0) {
        std::cerr << "[fas_serial] tcgetattr: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    // Raw mode — no processing, 8N1.
    ::cfmakeraw(&tty);
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);  // 1 stop bit
    tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS); // no HW flow control

    // Map baud. The FMC link runs at 460800; standard termios supports it on Linux.
    speed_t speed = B115200;
    switch (baud_) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        case 230400: speed = B230400; break;
        case 460800: speed = B460800; break;
        case 921600: speed = B921600; break;
        default:
            std::cerr << "[fas_serial] unsupported baud " << baud_
                      << ", falling back to 115200\n";
            break;
    }
    ::cfsetispeed(&tty, speed);
    ::cfsetospeed(&tty, speed);

    // Block until at least 1 byte arrives (VMIN=1, VTIME=0).
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "[fas_serial] tcsetattr: " << std::strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    ::tcflush(fd_, TCIFLUSH);
    std::cout << "[fas_serial] opened " << port_ << " @ " << baud_ << " baud\n";
    return true;
}

void FasSerial::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool FasSerial::is_open() const {
    return fd_ >= 0;
}

void FasSerial::set_frame_callback(FrameCallback cb) {
    frame_cb_ = std::move(cb);
}

void FasSerial::run() {
    running_ = true;
    reset_parser();

    uint8_t byte = 0;
    while (running_) {
        ssize_t n = ::read(fd_, &byte, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[fas_serial] read error: " << std::strerror(errno) << "\n";
            break;
        }
        if (n == 0) continue; // timeout / no data (shouldn't happen with VMIN=1)
        step(byte);
    }
}

void FasSerial::stop() {
    running_ = false;
}

bool FasSerial::send_frame(uint32_t can_id, const uint8_t* data, size_t len) {
    if (fd_ < 0 || len > 8) return false;

    // Build payload: CAN_ID (4 bytes LE) + data
    uint8_t payload[4 + 8];
    payload[0] = static_cast<uint8_t>(can_id & 0xFF);
    payload[1] = static_cast<uint8_t>((can_id >> 8) & 0xFF);
    payload[2] = static_cast<uint8_t>((can_id >> 16) & 0xFF);
    payload[3] = static_cast<uint8_t>((can_id >> 24) & 0xFF);
    for (size_t i = 0; i < len; ++i) payload[4 + i] = data[i];

    uint16_t plen = static_cast<uint16_t>(4 + len);
    uint8_t hdr[2] = { static_cast<uint8_t>(plen & 0xFF),
                       static_cast<uint8_t>((plen >> 8) & 0xFF) };

    // CRC over hdr + payload
    uint8_t crc_input[2 + 4 + 8];
    crc_input[0] = hdr[0];
    crc_input[1] = hdr[1];
    for (size_t i = 0; i < plen; ++i) crc_input[2 + i] = payload[i];
    uint16_t crc = crc16(crc_input, 2 + plen);

    // Wire frame: 0xAA + hdr(2) + payload(plen) + crc(2)
    uint8_t frame[1 + 2 + 4 + 8 + 2];
    size_t  fi = 0;
    frame[fi++] = kMagic;
    frame[fi++] = hdr[0];
    frame[fi++] = hdr[1];
    for (size_t i = 0; i < plen; ++i) frame[fi++] = payload[i];
    frame[fi++] = static_cast<uint8_t>(crc & 0xFF);
    frame[fi++] = static_cast<uint8_t>((crc >> 8) & 0xFF);

    std::lock_guard<std::mutex> lock(write_mutex_);
    ssize_t written = ::write(fd_, frame, fi);
    return written == static_cast<ssize_t>(fi);
}

FasSerial::Stats FasSerial::stats() const {
    return {frames_decoded_, frames_dropped_};
}

// CRC-16/CCITT-FALSE — identical to rt_crc16() in rt_proto.c.
uint16_t FasSerial::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u)
                ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

// ---- Frame parser state machine -------------------------------------------
// Mirrors protocol.py FrameParser._step() exactly.

void FasSerial::reset_parser() {
    state_        = State::WAIT_MAGIC;
    expected_len_ = 0;
    buf_.clear();
    crc_lo_       = 0;
}

void FasSerial::step(uint8_t byte) {
    switch (state_) {
        case State::WAIT_MAGIC:
            if (byte == kMagic) state_ = State::WAIT_LEN_LO;
            return;

        case State::WAIT_LEN_LO:
            expected_len_ = byte;
            state_        = State::WAIT_LEN_HI;
            return;

        case State::WAIT_LEN_HI:
            expected_len_ |= static_cast<uint16_t>(byte) << 8;
            if (expected_len_ < 4 || expected_len_ > kMaxPayload) {
                ++frames_dropped_;
                reset_parser();
                return;
            }
            buf_.clear();
            buf_.reserve(expected_len_);
            state_ = State::READ_PAYLOAD;
            return;

        case State::READ_PAYLOAD:
            buf_.push_back(byte);
            if (buf_.size() == expected_len_) state_ = State::WAIT_CRC_LO;
            return;

        case State::WAIT_CRC_LO:
            crc_lo_ = byte;
            state_  = State::WAIT_CRC_HI;
            return;

        case State::WAIT_CRC_HI: {
            uint16_t crc_rx = static_cast<uint16_t>(crc_lo_) |
                              (static_cast<uint16_t>(byte) << 8);

            // CRC input = hdr(2) + payload
            uint8_t crc_input[2 + 4 + 8];
            crc_input[0] = static_cast<uint8_t>(expected_len_ & 0xFF);
            crc_input[1] = static_cast<uint8_t>((expected_len_ >> 8) & 0xFF);
            for (size_t i = 0; i < buf_.size(); ++i) crc_input[2 + i] = buf_[i];
            uint16_t crc_calc = crc16(crc_input, 2 + buf_.size());

            if (crc_rx != crc_calc) {
                ++frames_dropped_;
                reset_parser();
                return;
            }

            // Extract CAN ID (4 bytes LE) and data payload.
            uint32_t can_id = static_cast<uint32_t>(buf_[0])
                            | (static_cast<uint32_t>(buf_[1]) << 8)
                            | (static_cast<uint32_t>(buf_[2]) << 16)
                            | (static_cast<uint32_t>(buf_[3]) << 24);
            size_t data_len = buf_.size() - 4;

            ++frames_decoded_;
            if (frame_cb_) {
                frame_cb_(can_id, buf_.data() + 4, data_len);
            }
            reset_parser();
            return;
        }
    }
}

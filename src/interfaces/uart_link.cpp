#include "uart_link.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

namespace {
speed_t baud_to_termios(int baud_rate) {
    switch (baud_rate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
#ifdef B230400
    case 230400:
        return B230400;
#endif
#ifdef B460800
    case 460800:
        return B460800;
#endif
    default:
        return 0;
    }
}
}

UartLink::UartLink(std::string device, int baud_rate)
    : device_(std::move(device)), baud_rate_(baud_rate) {}

UartLink::~UartLink() {
    close();
}

bool UartLink::open() {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (fd_ >= 0) {
        return true;
    }

    fd_ = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (fd_ < 0) {
        std::cerr << "UART open failed for " << device_ << ": " << std::strerror(errno) << std::endl;
        return false;
    }

    if (!configure_port()) {
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    parser_.reset();
    return true;
}

void UartLink::close() {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool UartLink::is_open() const {
    std::lock_guard<std::mutex> lock(io_mutex_);
    return fd_ >= 0;
}

bool UartLink::send_frame(const UartFrame& frame) {
    std::vector<uint8_t> bytes = build_uart_frame(frame);
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (fd_ < 0) {
        std::cerr << "UART send requested while port is closed." << std::endl;
        return false;
    }

    size_t written = 0;
    while (written < bytes.size()) {
        ssize_t n = ::write(fd_, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "UART write failed: " << std::strerror(errno) << std::endl;
            return false;
        }
        written += static_cast<size_t>(n);
    }

    if (::tcdrain(fd_) != 0) {
        std::cerr << "UART drain failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}

std::optional<UartFrame> UartLink::read_frame(std::chrono::milliseconds timeout) {
    std::lock_guard<std::mutex> lock(io_mutex_);
    if (!pending_frames_.empty()) {
        UartFrame frame = pending_frames_.front();
        pending_frames_.pop_front();
        return frame;
    }

    if (fd_ < 0) {
        return std::nullopt;
    }

    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    int poll_result = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
    if (poll_result < 0) {
        if (errno != EINTR) {
            std::cerr << "UART poll failed: " << std::strerror(errno) << std::endl;
        }
        return std::nullopt;
    }
    if (poll_result == 0 || !(pfd.revents & POLLIN)) {
        return std::nullopt;
    }

    std::array<uint8_t, 128> buf{};
    ssize_t n = ::read(fd_, buf.data(), buf.size());
    if (n < 0) {
        if (errno != EINTR) {
            std::cerr << "UART read failed: " << std::strerror(errno) << std::endl;
        }
        return std::nullopt;
    }

    for (ssize_t i = 0; i < n; ++i) {
        UartFrame frame;
        if (parser_.feed(buf[static_cast<size_t>(i)], frame)) {
            if (uart_frame_basic_valid(frame)) {
                pending_frames_.push_back(frame);
            } else {
                std::cerr << "UART dropped invalid frame." << std::endl;
            }
        }
    }

    if (pending_frames_.empty()) {
        return std::nullopt;
    }

    UartFrame frame = pending_frames_.front();
    pending_frames_.pop_front();
    return frame;
}

const std::string& UartLink::device() const {
    return device_;
}

int UartLink::baud_rate() const {
    return baud_rate_;
}

bool UartLink::configure_port() {
    speed_t speed = baud_to_termios(baud_rate_);
    if (speed == 0) {
        std::cerr << "Unsupported UART baud rate: " << baud_rate_ << std::endl;
        return false;
    }

    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        std::cerr << "UART tcgetattr failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_iflag &= static_cast<tcflag_t>(~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON));
    tty.c_oflag &= static_cast<tcflag_t>(~OPOST);
    tty.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL | ICANON | ISIG | IEXTEN));
    tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tty.c_cflag |= CS8;
#ifdef CRTSCTS
    tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        std::cerr << "UART tcsetattr failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    if (::tcflush(fd_, TCIOFLUSH) != 0) {
        std::cerr << "UART flush failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    return true;
}

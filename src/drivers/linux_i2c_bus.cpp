#include "linux_i2c_bus.hpp"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {
// One SMBus byte-data transaction, the same ioctl wiringPi's
// wiringPiI2CReadReg8/WriteReg8 make.
int smbus_byte_data(int fd, char read_write, uint8_t reg, i2c_smbus_data* data) {
    i2c_smbus_ioctl_data args;
    args.read_write = read_write;
    args.command = reg;
    args.size = I2C_SMBUS_BYTE_DATA;
    args.data = data;
    return ioctl(fd, I2C_SMBUS, &args);
}
}

LinuxI2cBus::LinuxI2cBus(const char* device, Transfer transfer) : transfer_(transfer) {
    fd_ = open(device, O_RDWR);
    if (fd_ < 0) {
        set_error(std::string("Failed to open I2C bus ") + device + ": " + std::strerror(errno));
    }
}

LinuxI2cBus::~LinuxI2cBus() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool LinuxI2cBus::is_ready() const {
    return fd_ >= 0;
}

const std::string& LinuxI2cBus::last_error() const {
    return last_error_;
}

bool LinuxI2cBus::attach(uint8_t address) {
    if (fd_ < 0) {
        set_error("I2C device not ready");
        return false;
    }
    if (current_address_ == address) {
        return true;
    }
    if (ioctl(fd_, I2C_SLAVE, address) < 0) {
        set_error(describe("Failed to set I2C device address", address, -1));
        current_address_ = -1;
        return false;
    }
    current_address_ = address;
    return true;
}

bool LinuxI2cBus::write_reg8(uint8_t address, uint8_t reg, uint8_t value) {
    if (!attach(address)) {
        return false;
    }
    if (transfer_ == Transfer::Smbus) {
        i2c_smbus_data data;
        data.byte = value;
        if (smbus_byte_data(fd_, I2C_SMBUS_WRITE, reg, &data) < 0) {
            set_error(describe("Failed to write I2C register", address, reg));
            return false;
        }
        return true;
    }
    uint8_t buffer[2] = {reg, value};
    if (write(fd_, buffer, 2) != 2) {
        set_error(describe("Failed to write I2C register", address, reg));
        return false;
    }
    return true;
}

bool LinuxI2cBus::read_reg8(uint8_t address, uint8_t reg, uint8_t& value) {
    if (!attach(address)) {
        return false;
    }
    if (transfer_ == Transfer::Smbus) {
        i2c_smbus_data data;
        if (smbus_byte_data(fd_, I2C_SMBUS_READ, reg, &data) < 0) {
            set_error(describe("Failed to read from I2C register", address, reg));
            return false;
        }
        value = data.byte & 0xFF;
        return true;
    }
    if (write(fd_, &reg, 1) != 1) {
        set_error(describe("Failed to select I2C register", address, reg));
        return false;
    }
    if (read(fd_, &value, 1) != 1) {
        set_error(describe("Failed to read from I2C register", address, reg));
        return false;
    }
    return true;
}

// "Failed to ... at 0x40 register 0x00: No such device or address". Reads errno,
// so call it straight after the failing call.
std::string LinuxI2cBus::describe(const char* what, uint8_t address, int reg) {
    const int err = errno;
    char where[40];
    if (reg >= 0) {
        std::snprintf(where, sizeof where, " at 0x%02X register 0x%02X", address, reg);
    } else {
        std::snprintf(where, sizeof where, " 0x%02X", address);
    }
    return std::string(what) + where + ": " + std::strerror(err);
}

// A chip that stops answering would otherwise log every failed transaction,
// hundreds a second. A message is printed the first time, then at most once per
// kRepeatInterval while it keeps repeating, with how many were left out.
void LinuxI2cBus::set_error(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    last_error_ = message;
    const auto now = std::chrono::steady_clock::now();
    if (message == last_logged_ && now - last_logged_at_ < kRepeatInterval) {
        ++suppressed_;
        return;
    }
    std::cerr << message;
    if (message == last_logged_ && suppressed_ > 0) {
        std::cerr << " (" << suppressed_ << " more like this since the last one)";
    }
    std::cerr << std::endl;
    last_logged_ = message;
    last_logged_at_ = now;
    suppressed_ = 0;
}

#include "linux_i2c_bus.hpp"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <iostream>

LinuxI2cBus::LinuxI2cBus(const char* device) {
    fd_ = open(device, O_RDWR);
    if (fd_ < 0) {
        set_error("Failed to open I2C bus");
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
        set_error("Failed to set I2C device address");
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
    uint8_t buffer[2] = {reg, value};
    if (write(fd_, buffer, 2) != 2) {
        set_error("Failed to write I2C register");
        return false;
    }
    return true;
}

bool LinuxI2cBus::read_reg8(uint8_t address, uint8_t reg, uint8_t& value) {
    if (!attach(address)) {
        return false;
    }
    if (write(fd_, &reg, 1) != 1) {
        set_error("Failed to select I2C register");
        return false;
    }
    if (read(fd_, &value, 1) != 1) {
        set_error("Failed to read from I2C register");
        return false;
    }
    return true;
}

void LinuxI2cBus::set_error(const std::string& message) {
    last_error_ = message;
    std::cerr << message << std::endl;
}

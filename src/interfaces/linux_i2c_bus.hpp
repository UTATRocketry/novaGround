#pragma once

#include <cstdint>
#include <string>

#include "interfaces/i2c_bus.hpp"

// I2C through /dev/i2c-N with plain read()/write(): the same transactions the
// TCA9535 driver made before it was split from the bus.
class LinuxI2cBus : public I2cBus {
public:
    explicit LinuxI2cBus(const char* device);
    ~LinuxI2cBus() override;

    LinuxI2cBus(const LinuxI2cBus&) = delete;
    LinuxI2cBus& operator=(const LinuxI2cBus&) = delete;

    bool attach(uint8_t address) override;
    bool write_reg8(uint8_t address, uint8_t reg, uint8_t value) override;
    bool read_reg8(uint8_t address, uint8_t reg, uint8_t& value) override;

    bool is_ready() const override;
    const std::string& last_error() const override;

private:
    int fd_ = -1;
    int current_address_ = -1;
    std::string last_error_;

    void set_error(const std::string& message);
};

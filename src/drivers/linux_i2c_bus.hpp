#pragma once

#include <cstdint>
#include <string>

#include "interfaces/i2c_bus.hpp"

// I2C through /dev/i2c-N.
//
// Two transfer styles, because the two chips were driven differently before
// the bus was split out and a refactor must not change what a chip sees:
//   Plain: write()/read() on the fd (the TCA9535 relay expander).
//   Smbus: I2C_SMBUS byte-data ioctls (the PCA9685, which wiringPi drove).
//          A register read is one repeated-start transaction.
//
// One object is one fd, so each chip gets its own LinuxI2cBus, as before.
class LinuxI2cBus : public I2cBus {
public:
    enum class Transfer { Plain, Smbus };

    explicit LinuxI2cBus(const char* device, Transfer transfer = Transfer::Plain);
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
    Transfer transfer_;
    std::string last_error_;

    void set_error(const std::string& message);
};

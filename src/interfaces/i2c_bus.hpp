#pragma once

#include <cstdint>
#include <string>

// The transport under the I2C chips (TCA9535 relay expander, later the
// PCA9685 servo driver). The real one talks to /dev/i2c-N; tests use a fake
// that records register writes and can inject failures.
class I2cBus {
public:
    virtual ~I2cBus() = default;

    // Make `address` the target of later transactions. False if the bus
    // cannot address it.
    virtual bool attach(uint8_t address) = 0;
    virtual bool write_reg8(uint8_t address, uint8_t reg, uint8_t value) = 0;
    virtual bool read_reg8(uint8_t address, uint8_t reg, uint8_t& value) = 0;

    virtual bool is_ready() const = 0;
    virtual const std::string& last_error() const = 0;
};

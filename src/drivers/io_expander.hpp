#pragma once

#include <bitset>
#include <chrono>
#include <cstdint>
#include <string>

#include "interfaces/i2c_bus.hpp"
#include "interfaces/relay_bank.hpp"

class TCA9535 : public RelayBank {
  public:
    // The bus must outlive this object. `retry_delay` is the pause between
    // write retries (50 ms on the Pi; tests pass zero).
    TCA9535(I2cBus& bus, uint8_t address,
            std::chrono::milliseconds retry_delay = std::chrono::milliseconds(50));

    bool is_ready() const override;
    const std::string& last_error() const;

    bool configure_port(uint8_t port, uint8_t direction);
    bool write_output(const std::bitset<16>& state) override;
    bool read_input(uint8_t port, uint8_t& value);

  private:
    I2cBus& bus_;
    uint8_t device_address = 0;
    std::chrono::milliseconds retry_delay_;
    bool ready_ = false;
    std::string last_error_;

    enum Registers {
        INPUT_PORT0 = 0x00,
        INPUT_PORT1 = 0x01,
        OUTPUT_PORT0 = 0x02,
        OUTPUT_PORT1 = 0x03,
        POLARITY_INV0 = 0x04,
        POLARITY_INV1 = 0x05,
        CONFIG_PORT0 = 0x06,
        CONFIG_PORT1 = 0x07
    };

    bool write_register(uint8_t reg, uint8_t value);
    bool read_register(uint8_t reg, uint8_t& value);
    void set_error(const std::string& message);
};

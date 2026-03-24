#pragma once

#include <bitset>
#include <cstdint>
#include <iostream>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

class TCA9535 {
  public:
    TCA9535(const char* i2c_bus, uint8_t address);
    ~TCA9535();

    bool is_ready() const;
    const std::string& last_error() const;

    bool configure_port(uint8_t port, uint8_t direction);
    bool write_output(const std::bitset<16>& state);
    bool read_input(uint8_t port, uint8_t& value);

  private:
    int i2c_fd = -1;
    uint8_t device_address = 0;
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

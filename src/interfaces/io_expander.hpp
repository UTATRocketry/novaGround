#pragma once

#include <bitset>
#include <cstdint>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>

class TCA9535 {
  private:
    int i2c_fd;
    uint8_t device_address;

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

  public:
    TCA9535(const char* i2c_bus, uint8_t address);
    ~TCA9535();

    void configure_port(uint8_t port, uint8_t direction);
    void write_output(std::bitset<16> state);
    uint8_t read_input(uint8_t port);

  private:
    void write_register(uint8_t reg, uint8_t value);
    uint8_t read_register(uint8_t reg);
};

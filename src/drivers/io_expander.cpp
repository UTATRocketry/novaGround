#include "io_expander.hpp"

#include <iostream>
#include <thread>

TCA9535::TCA9535(I2cBus& bus, uint8_t address, std::chrono::milliseconds retry_delay)
    : bus_(bus), device_address(address), retry_delay_(retry_delay) {
    if (!bus_.is_ready()) {
        set_error("Failed to open I2C bus");
        return;
    }
    if (!bus_.attach(device_address)) {
        set_error("Failed to set I2C device address");
        return;
    }
    ready_ = true;
}

bool TCA9535::is_ready() const {
    return ready_ && bus_.is_ready();
}

const std::string& TCA9535::last_error() const {
    return last_error_;
}

bool TCA9535::configure_port(uint8_t port, uint8_t direction) {
    if (!is_ready()) {
        return false;
    }
    uint8_t config_reg = (port == 0) ? CONFIG_PORT0 : CONFIG_PORT1;
    return write_register(config_reg, direction);
}

bool TCA9535::write_output(const std::bitset<16>& state) {
    if (!is_ready()) {
        return false;
    }

    uint16_t value = static_cast<uint16_t>(state.to_ulong());
    bool ok0 = write_register(OUTPUT_PORT0, value & 0xFF);
    bool ok1 = write_register(OUTPUT_PORT1, (value >> 8) & 0xFF);

    if (ok0 && ok1) {
        return true;
    }

    std::cerr << "Issue writing relay state. Retrying..." << std::endl;
    for (int retries = 0; retries < 10; retries++) {
        ok0 = write_register(OUTPUT_PORT0, value & 0xFF);
        ok1 = write_register(OUTPUT_PORT1, (value >> 8) & 0xFF);
        if (ok0 && ok1) {
            std::cout << "Relay state written successfully after retry." << std::endl;
            return true;
        }
        std::this_thread::sleep_for(retry_delay_);
    }

    std::cerr << "Failed to write relay state after retries." << std::endl;
    return false;
}

bool TCA9535::read_input(uint8_t port, uint8_t& value) {
    if (!is_ready()) {
        return false;
    }
    uint8_t input_reg = (port == 0) ? INPUT_PORT0 : INPUT_PORT1;
    return read_register(input_reg, value);
}

bool TCA9535::write_register(uint8_t reg, uint8_t value) {
    if (!bus_.write_reg8(device_address, reg, value)) {
        set_error("Failed to write I2C register");
        return false;
    }
    return true;
}

bool TCA9535::read_register(uint8_t reg, uint8_t& value) {
    if (!bus_.read_reg8(device_address, reg, value)) {
        set_error("Failed to read I2C register");
        return false;
    }
    return true;
}

void TCA9535::set_error(const std::string& message) {
    last_error_ = message;
    std::cerr << message << std::endl;
}

#include "io_expander.hpp"

#include <cstdio>
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
    // Selecting an address succeeds whether or not a chip is there, so read a
    // register to find out. A missing chip stays not ready and is never touched
    // again.
    uint8_t config = 0;
    if (!bus_.read_reg8(device_address, CONFIG_PORT0, config)) {
        char addr[8];
        std::snprintf(addr, sizeof addr, "0x%02X", device_address);
        set_error(std::string("TCA9535 relay board: no answer at I2C address ") + addr + " (" +
                  bus_.last_error() + ")");
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
    std::lock_guard<std::mutex> lock(mutex_);
    return write_register(config_reg, direction);
}

bool TCA9535::write_output(const std::bitset<16>& state) {
    if (!is_ready()) {
        return false;
    }

    uint16_t value = static_cast<uint16_t>(state.to_ulong());
    std::lock_guard<std::mutex> lock(mutex_);
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

bool TCA9535::read_output(std::bitset<16>& state) {
    if (!is_ready()) {
        return false;
    }
    // Reads the output registers: what the chip was last told to drive. (The
    // input registers would give the pin levels, which this does not need.)
    uint8_t port0 = 0;
    uint8_t port1 = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!read_register(OUTPUT_PORT0, port0) || !read_register(OUTPUT_PORT1, port1)) {
        return false;
    }
    state = std::bitset<16>(static_cast<unsigned long>(port0) |
                            (static_cast<unsigned long>(port1) << 8));
    return true;
}

bool TCA9535::read_input(uint8_t port, uint8_t& value) {
    if (!is_ready()) {
        return false;
    }
    uint8_t input_reg = (port == 0) ? INPUT_PORT0 : INPUT_PORT1;
    std::lock_guard<std::mutex> lock(mutex_);
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

// A repeated failure (a board that stopped answering, read back every state
// tick) is logged the first time and then at most every 10 s.
void TCA9535::set_error(const std::string& message) {
    const auto now = std::chrono::steady_clock::now();
    const bool repeat = message == last_error_ && now - last_logged_at_ < std::chrono::seconds(10);
    last_error_ = message;
    if (repeat) {
        return;
    }
    std::cerr << message << std::endl;
    last_logged_at_ = now;
}

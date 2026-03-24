#include "io_expander.hpp"

TCA9535::TCA9535(const char* i2c_bus, uint8_t address)
    : device_address(address) {
    i2c_fd = open(i2c_bus, O_RDWR);
    if (i2c_fd < 0) {
        set_error("Failed to open I2C bus");
        return;
    }

    if (ioctl(i2c_fd, I2C_SLAVE, device_address) < 0) {
        set_error("Failed to set I2C device address");
        close(i2c_fd);
        i2c_fd = -1;
        return;
    }

    ready_ = true;
}

TCA9535::~TCA9535() {
    if (i2c_fd >= 0) {
        close(i2c_fd);
    }
}

bool TCA9535::is_ready() const {
    return ready_ && i2c_fd >= 0;
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
        usleep(50000);
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
    if (i2c_fd < 0) {
        set_error("I2C device not ready");
        return false;
    }

    uint8_t buffer[2] = {reg, value};
    if (write(i2c_fd, buffer, 2) != 2) {
        set_error("Failed to write I2C register");
        return false;
    }
    return true;
}

bool TCA9535::read_register(uint8_t reg, uint8_t& value) {
    if (i2c_fd < 0) {
        set_error("I2C device not ready");
        return false;
    }

    if (write(i2c_fd, &reg, 1) != 1) {
        set_error("Failed to select I2C register");
        return false;
    }
    if (read(i2c_fd, &value, 1) != 1) {
        set_error("Failed to read from I2C register");
        return false;
    }
    return true;
}

void TCA9535::set_error(const std::string& message) {
    last_error_ = message;
    std::cerr << message << std::endl;
}

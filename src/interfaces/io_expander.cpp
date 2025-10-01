
#include "io_expander.hpp"

bool write_failed = false;

TCA9535::TCA9535(const char* i2c_bus, uint8_t address) : device_address(address) {
    // Open I2C bus
    i2c_fd = open(i2c_bus, O_RDWR);
    if (i2c_fd < 0) {
        throw std::runtime_error("Failed to open I2C bus");
    }

    // Set device address
    if (ioctl(i2c_fd, I2C_SLAVE, device_address) < 0) {
        close(i2c_fd);
        throw std::runtime_error("Failed to set I2C device address");
    }
}

TCA9535::~TCA9535() {
    close(i2c_fd);
}

void TCA9535::configure_port(uint8_t port, uint8_t direction) {
    uint8_t config_reg = (port == 0) ? CONFIG_PORT0 : CONFIG_PORT1;
    write_register(config_reg, direction);
}

void TCA9535::write_output(std::bitset<16> state) {
    bool success = false;
    std::cout << "Writing Relay States" << std::endl;

    uint16_t value = static_cast<uint16_t>(state.to_ulong());
    write_register(OUTPUT_PORT0, value & 0xFF);
    write_register(OUTPUT_PORT1, (value >> 8) & 0xFF);

    if (write_failed) {
        std::cerr << "Issue with writing relay state. Retrying..." << std::endl;
        
        for (int retries = 0; retries < 10; retries++) {
            write_register(OUTPUT_PORT0, value & 0xFF);
            write_register(OUTPUT_PORT1, (value >> 8) & 0xFF);

            if (!write_failed) {
                std::cout << "Relay state written successfully after retry." << std::endl;
                success = true;
                break;
            }
            usleep(50000);
        }
    
    }
    else {
        success = true;
    }
    if (!success) throw std::runtime_error("Failed to write relay state after 10 tries. Aborting program...");
}

uint8_t TCA9535::read_input(uint8_t port) {
    uint8_t input_reg = (port == 0) ? INPUT_PORT0 : INPUT_PORT1;
    return read_register(input_reg);
}

void TCA9535::write_register(uint8_t reg, uint8_t value) {
    write_failed = false;
    uint8_t buffer[2] = {reg, value};
    if (write(i2c_fd, buffer, 2) != 2) {
        write_failed = true;
    }
}

uint8_t TCA9535::read_register(uint8_t reg) {
    uint8_t value;
    if (write(i2c_fd, &reg, 1) != 1) {
        throw std::runtime_error("Failed to select I2C register");
    }
    if (read(i2c_fd, &value, 1) != 1) {
        throw std::runtime_error("Failed to read from I2C register");
    }
    return value;
}
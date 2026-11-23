#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "interfaces/i2c_bus.hpp"

// An I2cBus that is a register file: it records every write, answers reads
// from the registers, and can be told to fail.
class FakeI2cBus : public I2cBus {
public:
    struct Write {
        uint8_t address;
        uint8_t reg;
        uint8_t value;
    };

    bool ready = true;
    bool attach_ok = true;
    // The next `fail_writes` write attempts fail (and are still recorded).
    int fail_writes = 0;
    bool fail_reads = false;

    std::vector<Write> writes;
    std::vector<uint8_t> attached;
    std::map<std::pair<uint8_t, uint8_t>, uint8_t> registers;

    bool attach(uint8_t address) override {
        attached.push_back(address);
        return ready && attach_ok;
    }

    bool write_reg8(uint8_t address, uint8_t reg, uint8_t value) override {
        writes.push_back({address, reg, value});
        if (fail_writes > 0) {
            --fail_writes;
            error_ = "fake write failure";
            return false;
        }
        registers[{address, reg}] = value;
        return true;
    }

    bool read_reg8(uint8_t address, uint8_t reg, uint8_t& value) override {
        if (fail_reads) {
            error_ = "fake read failure";
            return false;
        }
        value = registers[{address, reg}];
        return true;
    }

    bool is_ready() const override { return ready; }
    const std::string& last_error() const override { return error_; }

    uint8_t reg(uint8_t address, uint8_t r) { return registers[{address, r}]; }

private:
    std::string error_;
};

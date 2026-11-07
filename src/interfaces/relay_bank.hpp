#pragma once

#include <bitset>

// What RelayController needs from the relay hardware: write all 16 outputs.
class RelayBank {
public:
    virtual ~RelayBank() = default;

    virtual bool is_ready() const = 0;
    virtual bool write_output(const std::bitset<16>& state) = 0;

    // Reads back what the hardware holds on the 16 outputs (the TCA9535's output
    // registers), bit i being relay i. Changes nothing. False if the read failed.
    virtual bool read_output(std::bitset<16>& state) = 0;
};

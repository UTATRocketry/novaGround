#pragma once

#include <bitset>

// What RelayController needs from the relay hardware: write all 16 outputs.
class RelayBank {
public:
    virtual ~RelayBank() = default;

    virtual bool is_ready() const = 0;
    virtual bool write_output(const std::bitset<16>& state) = 0;
};

#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "interfaces/digital_io.hpp"
#include "interfaces/pwm_driver.hpp"
#include "interfaces/relay_bank.hpp"

// Simulated outputs for novaMock: they behave like the real drivers (same
// contracts, same failure answers) but only keep state in memory, so the real
// controllers, logger and telemetry run on top of them with nothing attached.

class SimRelayBank : public RelayBank {
public:
    bool is_ready() const override { return true; }
    bool write_output(const std::bitset<16>& state) override;

    std::bitset<16> state() const;
    int writes() const;

private:
    mutable std::mutex mutex_;
    std::bitset<16> state_;
    int writes_ = 0;
};

class SimPwmDriver : public PwmDriver {
public:
    bool is_ready() const override { return true; }
    void writeMicroseconds(uint8_t channel, uint16_t microseconds) override;
    bool disablePWM(uint8_t channel) override;

    // 0 when the channel is off or was never written.
    uint16_t microseconds(uint8_t channel) const;

private:
    mutable std::mutex mutex_;
    std::map<uint8_t, uint16_t> pulse_us_;
};

// Pins start unconfigured. Like the real driver: write needs an output pin,
// read an input pin. An input pin reads the level given for it (default 0).
class SimDigitalIo : public DigitalIo {
public:
    explicit SimDigitalIo(std::map<int, int> input_levels = {});

    bool set_direction(int pin, const std::string& direction) override;
    bool write(int pin, int value) override;
    bool read(int pin, int& value) override;
    std::vector<int> get_input_pins() override;
    std::map<int, int> read_all_inputs() override;

    int output_level(int pin) const;  // last value written, 0 if never written

private:
    mutable std::mutex mutex_;
    std::map<int, std::string> directions_;
    std::map<int, int> input_levels_;
    std::map<int, int> output_levels_;
};

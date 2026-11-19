#pragma once

#include <bitset>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "interfaces/digital_io.hpp"
#include "interfaces/pwm_driver.hpp"
#include "interfaces/relay_bank.hpp"

class StatsRegistry;

// --stop-actuate: the three output drivers wrapped so that every write is
// dropped and counted, while everything else (the controllers' checks, their
// telemetry and log updates, the read-backs) runs exactly as normal. Commands
// still travel the whole path, so their timing is real, but no output moves.
//
// The wrappers sit under the controllers, at the driver seam, so no controller
// has a "stop" branch of its own and a new write added to a controller is
// stopped without anyone remembering to. Reads go to the wrapped driver, so
// nova/state/<node> keeps reporting what the hardware really holds (not what
// was commanded), which is how a bench script can see that nothing moved.
//
// A dropped write is reported as successful, so the controller carries on as it
// would have. Each one is counted in `stats` when it is set (null is fine). The
// wrapped driver and `stats` are not owned and must outlive the wrapper.

class StoppedRelayBank : public RelayBank {
public:
    StoppedRelayBank(RelayBank& inner, StatsRegistry* stats) : inner_(inner), stats_(stats) {}

    bool is_ready() const override { return inner_.is_ready(); }
    bool write_output(const std::bitset<16>& state) override;
    bool read_output(std::bitset<16>& state) override { return inner_.read_output(state); }

private:
    RelayBank& inner_;
    StatsRegistry* stats_;
};

class StoppedPwmDriver : public PwmDriver {
public:
    StoppedPwmDriver(PwmDriver& inner, StatsRegistry* stats) : inner_(inner), stats_(stats) {}

    bool is_ready() const override { return inner_.is_ready(); }
    void writeMicroseconds(uint8_t channel, uint16_t microseconds) override;
    bool disablePWM(uint8_t channel) override;
    bool read_channel(uint8_t channel, PwmChannelReadback& readback) override {
        return inner_.read_channel(channel, readback);
    }

private:
    PwmDriver& inner_;
    StatsRegistry* stats_;
};

// Changing a pin's direction is dropped too: a pin switched to output starts
// driving its line.
class StoppedDigitalIo : public DigitalIo {
public:
    StoppedDigitalIo(DigitalIo& inner, StatsRegistry* stats) : inner_(inner), stats_(stats) {}

    bool set_direction(int pin, const std::string& direction) override;
    bool write(int pin, int value) override;
    bool read(int pin, int& value) override { return inner_.read(pin, value); }
    std::vector<int> get_input_pins() override { return inner_.get_input_pins(); }
    std::map<int, int> read_all_inputs() override { return inner_.read_all_inputs(); }
    std::map<int, int> read_all_outputs() override { return inner_.read_all_outputs(); }

private:
    DigitalIo& inner_;
    StatsRegistry* stats_;
};

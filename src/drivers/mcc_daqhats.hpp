#pragma once

#include <vector>

#include "interfaces/analog_io.hpp"

// MCC 128 (analog) and MCC 134 (thermocouple) HATs through libdaqhats.
class MccDaqHats : public AnalogIo {
public:
    std::vector<DaqHatDevice> discover() override;
    bool open(const DaqHatDevice& hat) override;
    bool read(const DaqHatDevice& hat, int channel, double& value) override;
    bool set_tc_type(int address, int channel, TcType type) override;

private:
    // Addresses of the MCC 134s that opened.
    std::vector<int> mcc134_open_;
};

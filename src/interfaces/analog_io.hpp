#pragma once

#include <vector>

// One DAQ HAT found on the bus. `channels` is filled in by the driver that
// discovered it (empty for a HAT type the driver does not implement).
struct DaqHatDevice {
    int address = 0;
    int hat_id = 0;
    std::vector<int> channels;
};

// What the sampler needs from the analog hardware. Named AnalogIo, not
// AnalogInput, in case outputs are ever added.
class AnalogIo {
public:
    virtual ~AnalogIo() = default;

    // Throws std::runtime_error if the bus cannot be listed.
    virtual std::vector<DaqHatDevice> discover() = 0;
    virtual bool open(const DaqHatDevice& hat) = 0;
    virtual bool read(const DaqHatDevice& hat, int channel, double& value) = 0;
};

// Find the HATs on the bus and open each; returns the ones that opened. A
// failure to list the bus is logged and gives an empty list.
std::vector<DaqHatDevice> discover_and_open(AnalogIo& daq);

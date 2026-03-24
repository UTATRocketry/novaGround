#pragma once

#include <vector>

#include "core/telemetry.hpp"

std::vector<int> initialize_daqs();
bool get_daq_value(int address, int channel, double& value);

// Data sampling loop
void sample_func(const std::vector<int>& daq_hats,
                 const std::vector<int>& daq_channels,
                 TelemetryStore& store);

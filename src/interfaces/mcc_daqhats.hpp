#pragma once

#include <string>
#include <vector>

#include "core/telemetry.hpp"
#include "core/data_logger.hpp"

struct DaqHatDevice {
    int address = 0;
    int hat_id = 0;
    std::vector<int> channels;
};

std::vector<DaqHatDevice> initialize_daqs();
bool open_daq_hat(const DaqHatDevice& hat);
bool get_daq_value(const DaqHatDevice& hat, int channel, double& value);
std::vector<std::string> build_sensor_headers(const std::vector<DaqHatDevice>& hats);

// Data sampling loop
void sample_func(const std::vector<DaqHatDevice>& daq_hats,
                 TelemetryStore& store,
                 DataLogger* logger,
                 int sample_interval_ms);

#include "mcc128_daq.hpp"

#include <chrono>
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

std::vector<int> initialize_daqs() {
    std::vector<int> connected_daqs;
    int count = hat_list(HAT_ID_ANY, NULL);

    if (count < 0) {
        throw std::runtime_error("Error listing DAQ hats");
    }

    if (count > 0) {
        struct HatInfo* list =
            static_cast<HatInfo*>(malloc(count * sizeof(struct HatInfo)));
        hat_list(HAT_ID_ANY, list);

        for (int i = 0; i < count; i++) {
            connected_daqs.push_back(+list[i].address);
            std::cout << "Detected DAQ Hat at address: "
                      << list[i].address << std::endl;
        }

        free(list);
    }

    return connected_daqs;
}

bool get_daq_value(int address, int channel, double& value) {
    uint32_t options = OPTS_DEFAULT;
    int result = mcc128_a_in_read(address, channel, options, &value);
    if (result != RESULT_SUCCESS) {
        std::cerr << "DAQ read failed (addr=" << address
                  << ", channel=" << channel
                  << ", code=" << result << ")\n";
        return false;
    }
    return true;
}

void sample_func(const std::vector<int>& daq_hats,
                 const std::vector<int>& daq_channels,
                 TelemetryStore& store,
                 DataLogger* logger) {
    while (true) {
        std::vector<SensorSample> new_data;
        std::vector<double> values;
        new_data.reserve(daq_hats.size() * daq_channels.size());
        values.reserve(daq_hats.size() * daq_channels.size());

        auto now = std::chrono::system_clock::now();
        double timestamp_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

        for (int hat_id : daq_hats) {
            for (int channel : daq_channels) {
                double value = std::numeric_limits<double>::quiet_NaN();
                bool ok = get_daq_value(hat_id, channel, value);

                SensorSample sd;
                sd.hat_id = hat_id;
                sd.channel_id = channel;
                sd.value = ok ? value : std::numeric_limits<double>::quiet_NaN();
                sd.timestamp_ms = timestamp_ms;
                new_data.push_back(sd);
                values.push_back(sd.value);
            }
        }

        store.set_sensors(std::move(new_data));
        if (logger && logger->is_active()) {
            logger->log_sensor_row(timestamp_ms, values);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

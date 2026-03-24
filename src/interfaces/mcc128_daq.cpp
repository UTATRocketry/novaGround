#include "mcc128_daq.hpp"

#include <chrono>
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <iostream>
#include <thread>
#include <stdexcept>

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
                 TelemetryStore& store) {
    while (true) {
        std::vector<SensorSample> new_data;
        for (int hat_id : daq_hats) {
            for (int channel : daq_channels) {
                double value = 0.0;
                if (!get_daq_value(hat_id, channel, value)) {
                    continue;
                }

                SensorSample sd;
                sd.hat_id = hat_id;
                sd.channel_id = channel;
                sd.value = value;

                auto now = std::chrono::system_clock::now();
                sd.timestamp_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        now.time_since_epoch())
                        .count();

                new_data.push_back(sd);
            }
        }

        store.set_sensors(std::move(new_data));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


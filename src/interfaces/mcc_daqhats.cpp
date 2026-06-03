#include "mcc_daqhats.hpp"

#include <chrono>
#include <cstdlib>
#ifdef HAVE_DAQHATS
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <daqhats/mcc134.h>
#endif
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
#ifdef HAVE_DAQHATS
std::vector<int> channels_for_hat(int hat_id) {
    switch (hat_id) {
        case HAT_ID_MCC_128:
            return {0, 1, 2, 3, 4, 5, 6, 7};
        case HAT_ID_MCC_134:
            return {0, 1, 2, 3};
        default:
            return {};
    }
}

const char* hat_type_label(int hat_id) {
    switch (hat_id) {
        case HAT_ID_MCC_128:
            return "mcc128";
        case HAT_ID_MCC_134:
            return "mcc134";
        default:
            return "unknown";
    }
}
#endif
} // namespace

std::vector<DaqHatDevice> initialize_daqs() {
    std::vector<DaqHatDevice> connected_daqs;
#ifndef HAVE_DAQHATS
    std::cout << "MCC DAQHats library not available; skipping DAQ discovery." << std::endl;
    return connected_daqs;
#else
    int count = hat_list(HAT_ID_ANY, NULL);

    if (count < 0) {
        throw std::runtime_error("Error listing DAQ hats");
    }

    if (count > 0) {
        struct HatInfo* list =
            static_cast<HatInfo*>(malloc(count * sizeof(struct HatInfo)));
        hat_list(HAT_ID_ANY, list);

        for (int i = 0; i < count; i++) {
            DaqHatDevice device;
            device.address = list[i].address;
            device.hat_id = list[i].id;
            device.channels = channels_for_hat(device.hat_id);

            std::cout << "Detected DAQ Hat at address: " << device.address
                      << " (type=" << hat_type_label(device.hat_id) << ")"
                      << std::endl;

            if (device.channels.empty()) {
                std::cout << "DAQ Hat type not implemented yet (hat_id="
                          << device.hat_id << "); skipping setup for now."
                          << std::endl;
            }

            connected_daqs.push_back(device);
        }

        free(list);
    }

    return connected_daqs;
#endif
}

bool open_daq_hat(const DaqHatDevice& hat) {
#ifndef HAVE_DAQHATS
    (void)hat;
    std::cerr << "MCC DAQHats library not available; cannot open DAQ hat." << std::endl;
    return false;
#else
    int result = RESULT_SUCCESS;
    switch (hat.hat_id) {
        case HAT_ID_MCC_128:
            result = mcc128_open(hat.address);
            if (result != RESULT_SUCCESS) {
                std::cerr << "Failed to open MCC128 at address "
                          << hat.address << ": code " << result << std::endl;
                return false;
            }
            return true;
        case HAT_ID_MCC_134:
            result = mcc134_open(hat.address);
            if (result != RESULT_SUCCESS) {
                std::cerr << "Failed to open MCC134 at address "
                          << hat.address << ": code " << result << std::endl;
                return false;
            }
            for (int channel : hat.channels) {
                result = mcc134_tc_type_write(hat.address, channel, TC_TYPE_K);
                if (result != RESULT_SUCCESS) {
                    std::cerr << "Failed to set MCC134 TC type (addr="
                              << hat.address << ", channel=" << channel
                              << ", code=" << result << ")\n";
                    return false;
                }
            }
            return true;
        default:
            std::cerr << "DAQ Hat type not implemented yet (hat_id="
                      << hat.hat_id << "); skipping open." << std::endl;
            return false;
    }
#endif
}

bool get_daq_value(const DaqHatDevice& hat, int channel, double& value) {
#ifndef HAVE_DAQHATS
    (void)hat;
    (void)channel;
    value = std::numeric_limits<double>::quiet_NaN();
    return false;
#else
    int result = RESULT_SUCCESS;
    switch (hat.hat_id) {
        case HAT_ID_MCC_128: {
            uint32_t options = OPTS_DEFAULT;
            result = mcc128_a_in_read(hat.address, channel, options, &value);
            break;
        }
        case HAT_ID_MCC_134:
            result = mcc134_t_in_read(hat.address, channel, &value);
            break;
        default:
            std::cerr << "DAQ Hat type not implemented yet (hat_id="
                      << hat.hat_id << "); skipping read." << std::endl;
            return false;
    }

    if (result != RESULT_SUCCESS) {
        std::cerr << "DAQ read failed (addr=" << hat.address
                  << ", channel=" << channel
                  << ", code=" << result << ")\n";
        return false;
    }
    return true;
#endif
}

std::vector<std::string> build_sensor_headers(const std::vector<DaqHatDevice>& hats) {
    std::vector<std::string> headers;
    headers.push_back("timestamp");
    for (const auto& hat : hats) {
        if (hat.channels.empty()) {
            continue;
        }
        std::string prefix = "hat" + std::to_string(hat.address); // + "_" + std::string(hat_type_label(hat.hat_id));
        for (int channel : hat.channels) {
            headers.push_back(prefix + "_ch" + std::to_string(channel));
        }
    }
    return headers;
}

void sample_func(const std::vector<DaqHatDevice>& daq_hats,
                 TelemetryStore& store,
                 DataLogger* logger,
                 int sample_interval_ms) {
    int interval_ms = sample_interval_ms > 0 ? sample_interval_ms : 1;
    while (true) {
        std::vector<SensorSample> new_data;
        std::vector<double> values;
        size_t total_channels = 0;
        for (const auto& hat : daq_hats) {
            total_channels += hat.channels.size();
        }
        new_data.reserve(total_channels);
        values.reserve(total_channels);

        auto now = std::chrono::system_clock::now();
        double timestamp_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

        for (const auto& hat : daq_hats) {
            for (int channel : hat.channels) {
                double value = std::numeric_limits<double>::quiet_NaN();
                bool ok = get_daq_value(hat, channel, value);

                SensorSample sd;
                sd.hat_id = hat.address;
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

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

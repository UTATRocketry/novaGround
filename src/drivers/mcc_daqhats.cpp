#include "mcc_daqhats.hpp"

#include <cstdlib>
#include <daqhats/daqhats.h>
#include <daqhats/mcc128.h>
#include <daqhats/mcc134.h>
#include <iostream>
#include <stdexcept>

namespace {
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
} // namespace

std::vector<DaqHatDevice> MccDaqHats::discover() {
    std::vector<DaqHatDevice> connected_daqs;
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
}

bool MccDaqHats::open(const DaqHatDevice& hat) {
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
}

bool MccDaqHats::read(const DaqHatDevice& hat, int channel, double& value) {
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
}

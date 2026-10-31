#include "analog_io.hpp"

#include <exception>
#include <iostream>

std::vector<DaqHatDevice> discover_and_open(AnalogIo& daq) {
    std::vector<DaqHatDevice> opened;
    try {
        for (const auto& hat : daq.discover()) {
            if (daq.open(hat)) {
                opened.push_back(hat);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "DAQ initialization failed: " << e.what() << std::endl;
        opened.clear();
    }
    return opened;
}

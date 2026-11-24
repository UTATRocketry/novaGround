#include "analog_io.hpp"

#include <exception>
#include <iostream>
#include <string>

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

const char* tc_type_name(TcType type) {
    switch (type) {
        case TcType::J: return "J";
        case TcType::K: return "K";
        case TcType::T: return "T";
        case TcType::E: return "E";
        case TcType::R: return "R";
        case TcType::S: return "S";
        case TcType::B: return "B";
        case TcType::N: return "N";
        case TcType::Disabled: return "disabled";
    }
    return "?";
}

std::optional<TcType> parse_tc_type(const std::string& text) {
    if (text == "J") return TcType::J;
    if (text == "K") return TcType::K;
    if (text == "T") return TcType::T;
    if (text == "E") return TcType::E;
    if (text == "R") return TcType::R;
    if (text == "S") return TcType::S;
    if (text == "B") return TcType::B;
    if (text == "N") return TcType::N;
    return std::nullopt;
}

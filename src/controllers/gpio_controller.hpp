#pragma once

#include <boost/json.hpp>

#include "core/data_logger.hpp"
#include "interfaces/digital_io.hpp"

class GpioController {
public:
    GpioController(DigitalIo* gpio, DataLogger* logger);

    void handle_command(const boost::json::object& cmd);

private:
    bool available() const;

    DigitalIo* gpio_ = nullptr;
    DataLogger* logger_ = nullptr;
};

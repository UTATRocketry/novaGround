#pragma once

#include <boost/json.hpp>

#include "core/data_logger.hpp"

class DataFileController {
public:
    explicit DataFileController(DataLogger* logger);

    void handle_command(const boost::json::object& cmd);

private:
    DataLogger* logger_ = nullptr;
};

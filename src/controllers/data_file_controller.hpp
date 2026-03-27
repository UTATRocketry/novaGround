#pragma once

#include <boost/json.hpp>
#include <string>

#include "core/data_logger.hpp"

class DataFileController {
public:
    DataFileController(DataLogger* logger, std::string upload_url);

    void handle_command(const boost::json::object& cmd);

private:
    DataLogger* logger_ = nullptr;
    std::string upload_url_;
};

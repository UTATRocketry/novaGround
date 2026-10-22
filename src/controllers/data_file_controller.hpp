#pragma once

#include <boost/json.hpp>
#include <string>

#include "core/data_logger.hpp"

// Logs whether the backend's /health answers. Runs in the background and
// returns at once.
void check_backend(const std::string& backend_base);

class DataFileController {
public:
    DataFileController(DataLogger* logger, std::string upload_url);

    void handle_command(const boost::json::object& cmd);

private:
    DataLogger* logger_ = nullptr;
    std::string upload_url_;
};

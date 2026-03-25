#include "data_file_controller.hpp"

#include <iostream>

#include "utils/json_utils.hpp"

DataFileController::DataFileController(DataLogger* logger)
    : logger_(logger) {}

void DataFileController::handle_command(const boost::json::object& cmd) {
    if (!logger_) {
        std::cerr << "Data logger unavailable." << std::endl;
        return;
    }

    auto action_opt = get_string(cmd, "action");
    if (!action_opt) {
        std::cerr << "Data file command missing action." << std::endl;
        return;
    }

    std::string action = to_lower(*action_opt);
    if (action == "start_data_saving") {
        auto filename_opt = get_string(cmd, "filename");
        if (!filename_opt) {
            std::cerr << "Data file start missing filename." << std::endl;
            return;
        }
        if (!logger_->start(*filename_opt)) {
            std::cerr << "Failed to start data logging." << std::endl;
            return;
        }
        logger_->log_actuator_snapshot("start_data_saving");
        return;
    }

    if (action == "stop_data_saving") {
        logger_->stop();
        return;
    }

    std::cerr << "Unknown data file action: " << action << std::endl;
}

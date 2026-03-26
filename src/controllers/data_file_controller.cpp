#include "data_file_controller.hpp"

#include <filesystem>
#include <iostream>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

#include "utils/json_utils.hpp"

namespace {
const char* kUploadUrl = "http://localhost:8000/api/data-files/upload";

#ifdef HAVE_LIBCURL
bool upload_file(const std::string& path) {
    if (path.empty()) {
        std::cerr << "Upload skipped: empty path." << std::endl;
        return false;
    }
    if (!std::filesystem::exists(path)) {
        std::cerr << "Upload skipped: file not found: " << path << std::endl;
        return false;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Upload failed: curl init error." << std::endl;
        return false;
    }

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, path.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, kUploadUrl);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::cerr << "Upload failed for " << path << ": "
                  << curl_easy_strerror(res) << std::endl;
    }

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}
#else
bool upload_file(const std::string& path) {
    std::cerr << "Upload skipped (libcurl not available): " << path << std::endl;
    return false;
}
#endif
}

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
        const std::string sensor_path = logger_->last_sensor_path();
        const std::string actuator_path = logger_->last_actuator_path();
        upload_file(sensor_path);
        upload_file(actuator_path);
        return;
    }

    std::cerr << "Unknown data file action: " << action << std::endl;
}

#include "data_file_controller.hpp"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

#include "utils/json_utils.hpp"

namespace {

struct UploadResult {
    std::string file;
    bool ok = false;
    long http_status = 0;  // 0 when no HTTP response came back
    std::string error;
};

std::string utc_now_iso() {
    std::time_t now = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    return buf;
}

// Every outcome is one line starting with "UPLOAD", so a failure is easy to
// grep for in journalctl. A wrong backend port (issue #29) used to show up
// only as data that silently never arrived.
void log_result(const UploadResult& r, const std::string& upload_url) {
    if (r.ok) {
        std::cout << "UPLOAD OK " << r.file << " -> " << upload_url
                  << " (HTTP " << r.http_status << ")" << std::endl;
    } else {
        std::cerr << "UPLOAD FAILED " << r.file << " -> " << upload_url << ": " << r.error
                  << std::endl;
    }
}

// $NOVA_STATE_DIR/last_upload.json, which nova-pi.sh reports to the Nova
// Console. The systemd unit sets NOVA_STATE_DIR; without it nothing is written.
void write_last_upload(const std::vector<UploadResult>& results, const std::string& upload_url) {
    const char* dir = std::getenv("NOVA_STATE_DIR");
    if (!dir || !*dir) {
        return;
    }
    boost::json::array files;
    bool all_ok = !results.empty();
    for (const auto& r : results) {
        boost::json::object f;
        f["file"] = std::filesystem::path(r.file).filename().string();
        f["ok"] = r.ok;
        f["http"] = r.http_status;
        f["error"] = r.error;
        files.push_back(std::move(f));
        all_ok = all_ok && r.ok;
    }
    boost::json::object doc;
    doc["time"] = utc_now_iso();
    doc["url"] = upload_url;
    doc["ok"] = all_ok;
    doc["files"] = std::move(files);

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path target = std::filesystem::path(dir) / "last_upload.json";
    const std::filesystem::path tmp = std::filesystem::path(dir) / "last_upload.json.tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::cerr << "Could not write " << tmp << std::endl;
            return;
        }
        out << boost::json::serialize(doc) << "\n";
    }
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::cerr << "Could not write " << target << ": " << ec.message() << std::endl;
    }
}

#ifdef HAVE_LIBCURL
UploadResult upload_file(const std::string& path, const std::string& upload_url) {
    UploadResult r;
    r.file = path;
    if (path.empty()) {
        r.error = "no file was recorded";
        return r;
    }
    if (!std::filesystem::exists(path)) {
        r.error = "file not found";
        return r;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl init error";
        return r;
    }

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, path.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, upload_url.c_str());
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    // Without this, a 404 or 500 from the backend counts as a success.
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.http_status);
    r.ok = (res == CURLE_OK);
    if (!r.ok) {
        r.error = curl_easy_strerror(res);
        if (r.http_status > 0) {
            r.error += " (HTTP " + std::to_string(r.http_status) + ")";
        }
    }

    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    return r;
}

void check_backend_now(const std::string& backend_base) {
    const std::string url = backend_base + "/health";
    CURL* curl = curl_easy_init();
    if (!curl) {
        return;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        std::cout << "Backend " << backend_base << " answers." << std::endl;
    } else {
        std::cerr << "WARNING: backend " << backend_base << " does not answer ("
                  << curl_easy_strerror(res) << "). Data files will not upload until it does."
                  << std::endl;
    }
    curl_easy_cleanup(curl);
}
#else
UploadResult upload_file(const std::string& path, const std::string& upload_url) {
    (void)upload_url;
    UploadResult r;
    r.file = path;
    r.error = "built without libcurl";
    return r;
}

void check_backend_now(const std::string& backend_base) {
    std::cerr << "WARNING: built without libcurl, so data files can't be uploaded to "
              << backend_base << "." << std::endl;
}
#endif
}

void check_backend(const std::string& backend_base) {
    // In the background: the backend often comes up after the Pi, and startup
    // shouldn't wait for it.
    std::thread([backend_base] { check_backend_now(backend_base); }).detach();
}

DataFileController::DataFileController(DataLogger* logger, std::string upload_url)
    : logger_(logger),
      upload_url_(std::move(upload_url)) {}

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
        std::vector<UploadResult> results;
        for (const auto& path : {sensor_path, actuator_path}) {
            results.push_back(upload_file(path, upload_url_));
            log_result(results.back(), upload_url_);
        }
        write_last_upload(results, upload_url_);
        return;
    }

    std::cerr << "Unknown data file action: " << action << std::endl;
}

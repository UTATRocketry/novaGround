#include "data_file.hpp"

#include <cstdlib>
#include <iostream>

std::unique_ptr<DataFileWriter> DataFileWriter::from_env(const char* env_var) {
    const char* path = std::getenv(env_var);
    if (!path || std::string(path).empty()) {
        return nullptr;
    }
    return std::make_unique<DataFileWriter>(std::string(path));
}

DataFileWriter::DataFileWriter(const std::string& path) {
    file_.open(path, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "Failed to open data file: " << path << "\n";
        disabled_ = true;
    }
}

bool DataFileWriter::ok() const {
    return !disabled_ && file_.is_open();
}

void DataFileWriter::write_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ok()) {
        return;
    }
    file_ << line << "\n";
    if (!file_) {
        std::cerr << "Failed to write to data file. Disabling file output." << "\n";
        disabled_ = true;
    }
}

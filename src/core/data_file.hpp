#pragma once

#include <fstream>
#include <memory>
#include <mutex>
#include <string>

class DataFileWriter {
public:
    static std::unique_ptr<DataFileWriter> from_env(const char* env_var);

    explicit DataFileWriter(const std::string& path);
    bool ok() const;
    void write_line(const std::string& line);

private:
    std::mutex mutex_;
    std::ofstream file_;
    bool disabled_ = false;
};

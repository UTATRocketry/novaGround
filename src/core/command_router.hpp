#pragma once

#include <boost/json.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>

class StatsRegistry;

class CommandRouter {
public:
    using Handler = std::function<void(const boost::json::object&)>;

    explicit CommandRouter(std::string expected_source);

    void register_handler(const std::string& type, Handler handler);
    // Times every command that reaches a handler, when stats are on. Not owned;
    // must outlive the router. Call before commands arrive.
    void set_stats(StatsRegistry* stats) { stats_ = stats; }

    // `received` is when the message arrived, for the latency statistics.
    void handle_message(const std::string& payload,
                        std::chrono::steady_clock::time_point received = std::chrono::steady_clock::now());

private:
    std::string expected_source_;
    std::unordered_map<std::string, Handler> handlers_;
    StatsRegistry* stats_ = nullptr;

    void handle_command_object(const boost::json::object& cmd,
                               std::chrono::steady_clock::time_point received);
};

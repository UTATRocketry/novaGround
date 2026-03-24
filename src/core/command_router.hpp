#pragma once

#include <boost/json.hpp>
#include <functional>
#include <string>
#include <unordered_map>

class CommandRouter {
public:
    using Handler = std::function<void(const boost::json::object&)>;

    explicit CommandRouter(std::string expected_source);

    void register_handler(const std::string& type, Handler handler);
    void handle_message(const std::string& payload);

private:
    std::string expected_source_;
    std::unordered_map<std::string, Handler> handlers_;

    void handle_command_object(const boost::json::object& cmd);
};

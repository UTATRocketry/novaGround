#include "command_router.hpp"

#include <iostream>

namespace json = boost::json;

CommandRouter::CommandRouter(std::string expected_source)
    : expected_source_(std::move(expected_source)) {}

void CommandRouter::register_handler(const std::string& type, Handler handler) {
    handlers_[type] = std::move(handler);
}

void CommandRouter::handle_message(const std::string& payload) {
    boost::system::error_code ec;
    json::value parsed = json::parse(payload, ec);
    if (ec) {
        std::cerr << "Command parse failed: " << ec.message() << "\n";
        return;
    }

    if (!parsed.is_object()) {
        std::cerr << "Command payload is not a JSON object." << "\n";
        return;
    }

    const json::object& root = parsed.as_object();
    auto source_it = root.if_contains("source");
    if (!source_it || !source_it->is_string()) {
        std::cerr << "Command missing valid source." << "\n";
        return;
    }

    std::string source = source_it->as_string().c_str();
    if (source != expected_source_) {
        return;
    }

    json::object cmd_obj;
    if (auto cmd_it = root.if_contains("command")) {
        if (cmd_it->is_object()) {
            cmd_obj = cmd_it->as_object();
        } else if (cmd_it->is_string()) {
            cmd_obj["type"] = cmd_it->as_string();
            for (const auto& [key, value] : root) {
                if (key != "source" && key != "command") {
                    cmd_obj[key] = value;
                }
            }
        } else {
            std::cerr << "Command field is not an object or string." << "\n";
            return;
        }
    } else if (root.if_contains("type")) {
        cmd_obj = root;
    } else {
        std::cerr << "Command missing type." << "\n";
        return;
    }

    handle_command_object(cmd_obj);
}

void CommandRouter::handle_command_object(const json::object& cmd) {
    auto type_it = cmd.if_contains("type");
    if (!type_it || !type_it->is_string()) {
        std::cerr << "Command missing type string." << "\n";
        return;
    }

    std::string type = type_it->as_string().c_str();
    auto handler_it = handlers_.find(type);
    if (handler_it == handlers_.end()) {
        std::cerr << "Unhandled command type: " << type << "\n";
        return;
    }

    try {
        handler_it->second(cmd);
    } catch (const std::exception& e) {
        std::cerr << "Command handler error: " << e.what() << "\n";
    } catch (...) {
        std::cerr << "Command handler error: unknown exception" << "\n";
    }
}

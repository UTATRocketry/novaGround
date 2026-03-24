#include "json_utils.hpp"

#include <cctype>

std::string to_lower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::optional<int64_t> get_int(const boost::json::object& obj, const char* key) {
    auto it = obj.if_contains(key);
    if (!it) {
        return std::nullopt;
    }
    if (it->is_int64()) {
        return it->as_int64();
    }
    if (it->is_uint64()) {
        return static_cast<int64_t>(it->as_uint64());
    }
    if (it->is_double()) {
        return static_cast<int64_t>(it->as_double());
    }
    if (it->is_bool()) {
        return it->as_bool() ? 1 : 0;
    }
    return std::nullopt;
}

std::optional<bool> get_bool(const boost::json::object& obj, const char* key) {
    auto it = obj.if_contains(key);
    if (!it) {
        return std::nullopt;
    }
    if (it->is_bool()) {
        return it->as_bool();
    }
    if (it->is_int64()) {
        return it->as_int64() != 0;
    }
    if (it->is_uint64()) {
        return it->as_uint64() != 0;
    }
    if (it->is_double()) {
        return it->as_double() != 0.0;
    }
    return std::nullopt;
}

std::optional<std::string> get_string(const boost::json::object& obj, const char* key) {
    auto it = obj.if_contains(key);
    if (!it || !it->is_string()) {
        return std::nullopt;
    }
    return std::string(it->as_string().c_str());
}

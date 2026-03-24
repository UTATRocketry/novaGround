#pragma once

#include <boost/json.hpp>
#include <optional>
#include <string>

std::string to_lower(std::string value);
std::optional<int64_t> get_int(const boost::json::object& obj, const char* key);
std::optional<bool> get_bool(const boost::json::object& obj, const char* key);
std::optional<std::string> get_string(const boost::json::object& obj, const char* key);

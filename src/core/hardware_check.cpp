#include "core/hardware_check.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace json = boost::json;

namespace hardware_check {
namespace {

std::set<std::string> split_list(const std::string& text) {
    std::set<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ',')) {
        const auto first = item.find_first_not_of(" \t");
        if (first == std::string::npos) {
            continue;
        }
        const auto last = item.find_last_not_of(" \t");
        out.insert(item.substr(first, last - first + 1));
    }
    return out;
}

std::string hats_detail(const std::vector<DaqHatDevice>& hats) {
    std::string out;
    for (const auto& hat : hats) {
        if (!out.empty()) {
            out += ", ";
        }
        out += hat.thermocouples ? "MCC 134" : "MCC 128";
        out += " at " + std::to_string(hat.address);
    }
    return out;
}

}  // namespace

bool Report::gpio_ok() const {
    for (const auto& p : parts) {
        if (p.name == kGpio) {
            return p.found;
        }
    }
    return false;
}

std::vector<std::string> Report::missing() const {
    std::vector<std::string> out;
    for (const auto& p : parts) {
        if (p.expected && !p.found) {
            out.push_back(p.name);
        }
    }
    return out;
}

std::vector<std::string> Report::lines() const {
    std::vector<std::string> out;
    for (const auto& p : parts) {
        std::string line;
        if (p.found) {
            line = "  " + p.label + ": found";
            if (!p.detail.empty()) {
                line += " (" + p.detail + ")";
            }
            if (!p.expected) {
                line += ", not expected on " + build_id + "; using it";
            }
        } else if (p.expected) {
            line = "ERROR " + p.label + ": NOT FOUND, expected on " + build_id;
            if (p.name == kGpio) {
                line += ". Stopping: every build needs GPIO";
            } else {
                line += ". Carrying on without it";
            }
        } else {
            line = "  " + p.label + ": not fitted (not expected on " + build_id + ")";
        }
        out.push_back(line);
    }
    for (const auto& name : unknown_expected) {
        out.push_back("  (ignored unknown name '" + name + "' in this build's expected hardware)");
    }
    return out;
}

json::object Report::to_json() const {
    json::object root;
    root["build_id"] = build_id;
    json::object parts_json;
    for (const auto& p : parts) {
        json::object o;
        o["expected"] = p.expected;
        o["found"] = p.found;
        if (!p.detail.empty()) {
            o["detail"] = p.detail;
        }
        parts_json[p.name] = std::move(o);
    }
    root["parts"] = std::move(parts_json);
    json::array missing_json;
    for (const auto& name : missing()) {
        missing_json.push_back(json::value(name));
    }
    root["missing"] = std::move(missing_json);
    return root;
}

Report check(const std::string& build_id, const std::string& expected, const Found& found) {
    std::set<std::string> want = split_list(expected);
    want.insert(kGpio);

    Report r;
    r.build_id = build_id;
    auto add = [&](const char* name, const char* label, bool is_found, std::string detail) {
        Part p;
        p.name = name;
        p.label = label;
        p.expected = want.count(name) > 0;
        p.found = is_found;
        p.detail = std::move(detail);
        r.parts.push_back(std::move(p));
        want.erase(name);
    };
    add(kGpio, "GPIO (gpiochip0)", found.gpio, "");
    add(kDaq, "DAQ HATs", !found.daq_hats.empty(), hats_detail(found.daq_hats));
    add(kRelays, "relay board (TCA9535)", found.relays, "");
    add(kServo, "servo driver (PCA9685)", found.servo, "");
    r.unknown_expected.assign(want.begin(), want.end());
    return r;
}

}  // namespace hardware_check

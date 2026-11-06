#include "scenario.hpp"

#include <boost/json.hpp>
#include <fstream>
#include <sstream>

namespace json = boost::json;

namespace {
double as_double(const json::value& v, const std::string& what) {
    if (v.is_double()) return v.as_double();
    if (v.is_int64()) return static_cast<double>(v.as_int64());
    if (v.is_uint64()) return static_cast<double>(v.as_uint64());
    throw ScenarioError(what + " must be a number");
}

int as_int(const json::value& v, const std::string& what) {
    if (v.is_int64()) return static_cast<int>(v.as_int64());
    if (v.is_uint64()) return static_cast<int>(v.as_uint64());
    if (v.is_double()) return static_cast<int>(v.as_double());
    throw ScenarioError(what + " must be a number");
}

int key_as_int(const std::string& key, const std::string& what) {
    try {
        size_t used = 0;
        int n = std::stoi(key, &used);
        if (used == key.size()) return n;
    } catch (...) {
    }
    throw ScenarioError(what + " key '" + key + "' is not an integer");
}

const json::object& as_object(const json::value& v, const std::string& what) {
    if (!v.is_object()) throw ScenarioError(what + " must be an object");
    return v.as_object();
}

const json::array& as_array(const json::value& v, const std::string& what) {
    if (!v.is_array()) throw ScenarioError(what + " must be an array");
    return v.as_array();
}

Waveform parse_waveform(const json::value& v, const std::string& what) {
    const json::object& o = as_object(v, what);
    Waveform w;
    w.min = 0.0;
    w.max = 1.0;
    if (auto it = o.if_contains("wave")) {
        if (!it->is_string()) throw ScenarioError(what + ".wave must be a string");
        w.wave = std::string(it->as_string().c_str());
    }
    if (auto it = o.if_contains("min")) w.min = as_double(*it, what + ".min");
    if (auto it = o.if_contains("max")) w.max = as_double(*it, what + ".max");
    if (auto it = o.if_contains("period")) w.period = as_double(*it, what + ".period");
    if (auto it = o.if_contains("noise")) w.noise = as_double(*it, what + ".noise");
    w.value = (w.min + w.max) / 2.0;
    if (auto it = o.if_contains("value")) w.value = as_double(*it, what + ".value");
    return w;
}
}

Scenario parse_scenario(const std::string& json_text) {
    boost::system::error_code ec;
    json::value root = json::parse(json_text, ec);
    if (ec) throw ScenarioError("scenario is not valid JSON: " + ec.message());
    const json::object& top = as_object(root, "scenario");

    Scenario s;
    if (auto it = top.if_contains("seed")) s.seed = static_cast<uint32_t>(as_int(*it, "seed"));

    if (auto it = top.if_contains("hats")) {
        for (const json::value& hv : as_array(*it, "hats")) {
            const json::object& ho = as_object(hv, "hat");
            SimHat hat;
            auto addr = ho.if_contains("address");
            if (!addr) throw ScenarioError("hat is missing address");
            hat.address = as_int(*addr, "hat.address");
            auto type = ho.if_contains("type");
            if (!type || !type->is_string()) throw ScenarioError("hat is missing a string type");
            hat.type = std::string(type->as_string().c_str());
            if (hat.type != "mcc128" && hat.type != "mcc134") {
                throw ScenarioError("unknown hat type '" + hat.type + "' (use mcc128 or mcc134)");
            }
            if (auto ch = ho.if_contains("channels")) {
                for (const auto& [key, value] : as_object(*ch, "hat.channels")) {
                    std::string name(key.data(), key.size());
                    int channel = key_as_int(name, "channels");
                    hat.channels[channel] = parse_waveform(value, "channel " + name);
                }
            }
            s.hats.push_back(std::move(hat));
        }
    }

    if (auto it = top.if_contains("faults")) {
        const json::object& f = as_object(*it, "faults");
        if (auto re = f.if_contains("read_errors")) {
            for (const json::value& ev : as_array(*re, "faults.read_errors")) {
                const json::object& eo = as_object(ev, "read error");
                auto a = eo.if_contains("address");
                auto c = eo.if_contains("channel");
                if (!a || !c) throw ScenarioError("read error needs address and channel");
                s.read_errors.insert({as_int(*a, "read error address"), as_int(*c, "read error channel")});
            }
        }
        if (auto mh = f.if_contains("missing_hats")) {
            for (const json::value& av : as_array(*mh, "faults.missing_hats")) {
                s.missing_hats.insert(as_int(av, "missing hat address"));
            }
        }
        if (auto rd = f.if_contains("read_delay_us")) s.read_delay_us = as_int(*rd, "faults.read_delay_us");
    }

    if (auto it = top.if_contains("gpio_inputs")) {
        for (const auto& [key, value] : as_object(*it, "gpio_inputs")) {
            std::string name(key.data(), key.size());
            s.gpio_inputs[key_as_int(name, "gpio_inputs")] = as_int(value, "gpio input " + name);
        }
    }
    return s;
}

Scenario load_scenario_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ScenarioError("cannot open scenario file: " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_scenario(ss.str());
}

Scenario default_scenario() {
    auto sine = [](double lo, double hi, double period, double noise) {
        Waveform w;
        w.wave = "sine";
        w.min = lo;
        w.max = hi;
        w.period = period;
        w.noise = noise;
        w.value = (lo + hi) / 2.0;
        return w;
    };

    Scenario s;
    SimHat h0;
    h0.address = 0;
    h0.type = "mcc128";
    h0.channels[0] = sine(0.99, 3.99, 20, 0.02);   // PGSO
    h0.channels[1] = sine(0.99, 3.99, 20, 0.02);   // PGS
    h0.channels[4] = sine(1.98, 7.98, 25, 0.02);   // PVO
    h0.channels[6] = sine(3.59, 5.20, 30, 0.05);   // MOT
    h0.channels[7] = sine(3.59, 5.20, 30, 0.05);   // MFT
    s.hats.push_back(h0);

    SimHat h1;
    h1.address = 1;
    h1.type = "mcc128";
    h1.channels[1] = sine(1.925, 2.06, 18, 0.01);  // CC-LC
    s.hats.push_back(h1);
    return s;
}

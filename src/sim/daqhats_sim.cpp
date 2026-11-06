#include "daqhats_sim.hpp"

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "daqhats/mcc128.h"
#include "daqhats/mcc134.h"
#include "daqhats/daqhats.h"

namespace {
using Clock = std::chrono::steady_clock;

struct State {
    std::mutex mutex;
    bool loaded = false;
    bool load_failed = false;
    Scenario scenario;
    std::set<int> opened;
    std::map<std::pair<int, int>, int> tc_types;
    WaveformRng rng{1};
    Clock::time_point started = Clock::now();
    std::function<double()> clock;  // empty: use `started`

    double now() const {
        if (clock) return clock();
        return std::chrono::duration<double>(Clock::now() - started).count();
    }
};

State& state() {
    static State s;
    return s;
}

void install(State& s, const Scenario& scenario) {
    s.scenario = scenario;
    s.opened.clear();
    s.tc_types.clear();
    s.rng = WaveformRng(scenario.seed);
    s.started = Clock::now();
    s.loaded = true;
    s.load_failed = false;
}

// Caller holds the lock. False if there is no usable scenario.
bool ensure_loaded(State& s) {
    if (s.loaded) return true;
    if (s.load_failed) return false;
    const char* path = std::getenv("NOVA_SIM_SCENARIO");
    try {
        install(s, path && *path ? load_scenario_file(path) : default_scenario());
        if (path && *path) {
            std::cout << "libdaqhats_sim: scenario " << path << std::endl;
        } else {
            std::cout << "libdaqhats_sim: no NOVA_SIM_SCENARIO, using the default scenario" << std::endl;
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "libdaqhats_sim: cannot load scenario: " << e.what() << std::endl;
        s.load_failed = true;
        return false;
    }
}

uint16_t id_for_type(const std::string& type) {
    return type == "mcc134" ? HAT_ID_MCC_134 : HAT_ID_MCC_128;
}

// A hat that is present on the bus (in the scenario and not missing).
const SimHat* find_hat(const State& s, int address) {
    if (s.scenario.missing_hats.count(address)) return nullptr;
    for (const SimHat& h : s.scenario.hats) {
        if (h.address == address) return &h;
    }
    return nullptr;
}

int open_hat(uint8_t address, uint16_t expected_id) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensure_loaded(s)) return RESULT_UNDEFINED;
    const SimHat* hat = find_hat(s, address);
    if (!hat || id_for_type(hat->type) != expected_id) return RESULT_INVALID_DEVICE;
    s.opened.insert(address);
    return RESULT_SUCCESS;
}

int read_channel(uint8_t address, uint8_t channel, uint16_t expected_id, int channel_count,
                 double* value) {
    if (!value) return RESULT_BAD_PARAMETER;
    State& s = state();
    int delay_us = 0;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        if (!ensure_loaded(s)) return RESULT_UNDEFINED;
        const SimHat* hat = find_hat(s, address);
        if (!hat || id_for_type(hat->type) != expected_id || !s.opened.count(address)) {
            return RESULT_INVALID_DEVICE;
        }
        if (channel >= channel_count) return RESULT_BAD_PARAMETER;
        if (s.scenario.read_errors.count({address, channel})) return RESULT_COMMS_FAILURE;

        auto it = hat->channels.find(channel);
        *value = it == hat->channels.end() ? 0.0 : wave_value(it->second, s.now(), s.rng);
        delay_us = s.scenario.read_delay_us;
    }
    if (delay_us > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
    return RESULT_SUCCESS;
}
}  // namespace

namespace daqhats_sim {
void load(const Scenario& scenario) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    install(s, scenario);
}

void reset() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.loaded = false;
    s.load_failed = false;
    s.scenario = Scenario();
    s.opened.clear();
    s.tc_types.clear();
    s.clock = nullptr;
}

std::map<int, int> gpio_inputs() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensure_loaded(s)) return {};
    return s.scenario.gpio_inputs;
}

void set_clock(std::function<double()> seconds) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.clock = std::move(seconds);
}
}  // namespace daqhats_sim

extern "C" {

int hat_list(uint16_t filter_by_id, struct HatInfo* list) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensure_loaded(s)) return RESULT_UNDEFINED;

    int count = 0;
    for (const SimHat& hat : s.scenario.hats) {
        if (s.scenario.missing_hats.count(hat.address)) continue;
        const uint16_t id = id_for_type(hat.type);
        if (filter_by_id != HAT_ID_ANY && filter_by_id != id) continue;
        if (list) {
            HatInfo& info = list[count];
            std::memset(&info, 0, sizeof info);
            info.address = static_cast<uint8_t>(hat.address);
            info.id = id;
            info.version = 0;
            std::snprintf(info.product_name, sizeof info.product_name, "simulated %s", hat.type.c_str());
        }
        ++count;
    }
    return count;
}

int mcc128_open(uint8_t address) {
    return open_hat(address, HAT_ID_MCC_128);
}

int mcc128_a_in_read(uint8_t address, uint8_t channel, uint32_t /*options*/, double* value) {
    return read_channel(address, channel, HAT_ID_MCC_128, 8, value);
}

int mcc134_open(uint8_t address) {
    return open_hat(address, HAT_ID_MCC_134);
}

int mcc134_tc_type_write(uint8_t address, uint8_t channel, uint8_t type) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!ensure_loaded(s)) return RESULT_UNDEFINED;
    if (!s.opened.count(address)) return RESULT_INVALID_DEVICE;
    const SimHat* hat = find_hat(s, address);
    if (!hat || id_for_type(hat->type) != HAT_ID_MCC_134) return RESULT_INVALID_DEVICE;
    if (channel >= 4) return RESULT_BAD_PARAMETER;
    s.tc_types[{address, channel}] = type;
    return RESULT_SUCCESS;
}

int mcc134_t_in_read(uint8_t address, uint8_t channel, double* value) {
    return read_channel(address, channel, HAT_ID_MCC_134, 4, value);
}

}  // extern "C"

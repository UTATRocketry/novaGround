#include "output_supervisor.hpp"

#include <boost/json.hpp>
#include <iostream>

#include "core/stats.hpp"

namespace json = boost::json;
using namespace std::chrono;

namespace {
// A trip whose write failed is tried again after this long, not on every tick,
// so a dead driver does not fill the journal.
constexpr auto kTripRetry = seconds(1);
}  // namespace

std::string OutputId::name() const {
    return std::string(kind == OutputKind::Gpio ? "gpio:" : "relay:") + std::to_string(index);
}

OutputSupervisor::OutputSupervisor(SteadyClock steady, SystemMs system_ms)
    : steady_(steady ? std::move(steady) : SteadyClock([] { return steady_clock::now(); })),
      system_ms_(system_ms ? std::move(system_ms) : SystemMs([] {
          return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
      })) {}

void OutputSupervisor::set_trip_handler(TripHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    trip_handler_ = std::move(handler);
}

void OutputSupervisor::configure(std::map<OutputId, OutputLimits> limits,
                                 std::optional<std::int64_t> heartbeat_timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    limits_ = std::move(limits);
    if (heartbeat_timeout_ms && !heartbeat_timeout_ms_) {
        // Supervision starts now, with a full timeout before the first beat is due.
        last_beat_ = steady_();
        std::cout << "Comms-loss supervision on: heartbeat timeout " << *heartbeat_timeout_ms << " ms"
                  << std::endl;
    } else if (!heartbeat_timeout_ms && heartbeat_timeout_ms_) {
        std::cout << "Comms-loss supervision off: the config has no heartbeat section" << std::endl;
    }
    heartbeat_timeout_ms_ = heartbeat_timeout_ms;
}

void OutputSupervisor::heartbeat(const std::string& payload, bool retained) {
    boost::system::error_code ec;
    json::value v = json::parse(payload, ec);
    if (ec || !v.is_object()) {
        return;
    }
    const json::value* online = v.as_object().if_contains("online");
    if (!online || !online->is_bool()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!online->as_bool()) {
        if (!comms_lost_) {
            std::cerr << "Backend offline (its last will on nova/heartbeat/backend)" << std::endl;
        }
        comms_lost_ = true;
        return;
    }
    if (retained) {
        return;  // says the backend was up once, not that it is now
    }
    last_beat_ = steady_();
    comms_lost_ = false;
}

void OutputSupervisor::connection_lost() {
    std::lock_guard<std::mutex> lock(mutex_);
    comms_lost_ = true;
}

bool OutputSupervisor::stale_locked(steady_clock::time_point now) const {
    if (!heartbeat_timeout_ms_) {
        return false;
    }
    return comms_lost_ || now - last_beat_ > milliseconds(*heartbeat_timeout_ms_);
}

bool OutputSupervisor::comms_stale() {
    std::lock_guard<std::mutex> lock(mutex_);
    return stale_locked(steady_());
}

bool OutputSupervisor::allow_write(const OutputId& output, bool to_safe) {
    if (to_safe) {
        return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = limits_.find(output);
    if (it == limits_.end() || !it->second.safe_on_comms_loss) {
        return true;
    }
    return !stale_locked(steady_());
}

void OutputSupervisor::note_level(const OutputId& output, bool at_safe) {
    std::lock_guard<std::mutex> lock(mutex_);
    Level& level = levels_[output];  // a new entry is at its safe level
    if (!at_safe && level.at_safe) {
        level.left_safe = steady_();
    }
    level.at_safe = at_safe;
    if (at_safe) {
        trip_attempts_.erase(output);
    }
}

void OutputSupervisor::forget(const OutputId& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    levels_.erase(output);
    trip_attempts_.erase(output);
    if (trips_.erase(output)) {
        changed_ = true;
    }
}

void OutputSupervisor::command_applied(const OutputId& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (trips_.erase(output)) {
        changed_ = true;
        std::cout << "Trip cleared: " << output.name() << " (a command for it arrived)" << std::endl;
    }
}

void OutputSupervisor::record_trip(const OutputId& output, const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        trips_[output] = Trip{output, reason, system_ms_()};
        changed_ = true;
    }
    std::cerr << "TRIP " << output.name() << " returned to its safe level (" << reason << ")" << std::endl;
    if (stats_) {
        stats_->count_output_trip();
    }
}

void OutputSupervisor::tick() {
    std::vector<std::pair<OutputId, std::string>> due;
    TripHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = steady_();
        const bool stale = stale_locked(now);
        if (stale != was_stale_) {
            if (stale) {
                std::cerr << "Comms loss: no heartbeat from the backend (outputs set to 'safe' go to their "
                             "safe level, and commands that would move them off it are refused)"
                          << std::endl;
            } else {
                std::cout << "Comms restored: heartbeat from the backend" << std::endl;
            }
            was_stale_ = stale;
        }
        for (const auto& [output, limits] : limits_) {
            auto level = levels_.find(output);
            if (level == levels_.end() || level->second.at_safe) {
                continue;
            }
            const char* reason = nullptr;
            if (stale && limits.safe_on_comms_loss) {
                reason = "comms_loss";
            } else if (limits.max_on_ms && now - level->second.left_safe >= milliseconds(*limits.max_on_ms)) {
                reason = "max_on_time";
            }
            if (!reason) {
                continue;
            }
            // Still off safe after a trip was tried: its write failed. Try
            // again, but not on every tick.
            auto tried = trip_attempts_.find(output);
            if (tried != trip_attempts_.end() && now - tried->second < kTripRetry) {
                continue;
            }
            trip_attempts_[output] = now;
            due.emplace_back(output, reason);
        }
        handler = trip_handler_;
    }
    if (!handler) {
        return;
    }
    for (const auto& [output, reason] : due) {
        try {
            handler(output, reason);
        } catch (const std::exception& e) {
            std::cerr << "Trip of " << output.name() << " failed: " << e.what() << std::endl;
        }
    }
}

std::vector<Trip> OutputSupervisor::tripped() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Trip> out;
    out.reserve(trips_.size());
    for (const auto& [output, trip] : trips_) {
        out.push_back(trip);
    }
    return out;
}

bool OutputSupervisor::take_changed() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool changed = changed_;
    changed_ = false;
    return changed;
}

// ── wrappers ─────────────────────────────────────────────────────────────────

namespace {
void refused(const OutputId& output, StatsRegistry* stats) {
    std::cerr << "Refused: " << output.name()
              << " stays at its safe level while the backend heartbeat is lost (on_comms_loss: safe)"
              << std::endl;
    if (stats) {
        stats->count_refused_command();
    }
}
}  // namespace

bool SupervisedDigitalIo::configure_output(int pin, int level) {
    const OutputId output = gpio_output(pin);
    const bool to_safe = level == kGpioSafeLevel;
    if (!supervisor_.allow_write(output, to_safe)) {
        refused(output, stats_);
        return false;
    }
    if (!inner_.configure_output(pin, level)) {
        supervisor_.forget(output);  // a failed claim leaves the pin unclaimed
        return false;
    }
    supervisor_.note_level(output, to_safe);
    return true;
}

bool SupervisedDigitalIo::configure_input(int pin) {
    const bool ok = inner_.configure_input(pin);
    supervisor_.forget(gpio_output(pin));  // not an output, claimed or not
    return ok;
}

bool SupervisedDigitalIo::release(int pin) {
    const bool ok = inner_.release(pin);
    supervisor_.forget(gpio_output(pin));
    return ok;
}

bool SupervisedDigitalIo::write(int pin, int value) {
    const OutputId output = gpio_output(pin);
    const bool to_safe = value == kGpioSafeLevel;
    if (!supervisor_.allow_write(output, to_safe)) {
        refused(output, stats_);
        return false;
    }
    if (!inner_.write(pin, value)) {
        return false;
    }
    supervisor_.note_level(output, to_safe);
    return true;
}

SupervisedRelayBank::SupervisedRelayBank(RelayBank& inner,
                                         OutputSupervisor& supervisor,
                                         const std::bitset<16>& initial,
                                         StatsRegistry* stats)
    : inner_(inner), supervisor_(supervisor), stats_(stats), last_(initial) {
    for (int channel = 0; channel < 16; ++channel) {
        supervisor_.note_level(relay_output(channel), initial.test(channel) == kRelaySafeBit);
    }
}

bool SupervisedRelayBank::write_output(const std::bitset<16>& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::bitset<16> moving = state ^ last_;
    for (int channel = 0; channel < 16; ++channel) {
        if (moving.test(channel) && state.test(channel) != kRelaySafeBit &&
            !supervisor_.allow_write(relay_output(channel), false)) {
            refused(relay_output(channel), stats_);
            return false;
        }
    }
    if (!inner_.write_output(state)) {
        return false;
    }
    for (int channel = 0; channel < 16; ++channel) {
        if (moving.test(channel)) {
            supervisor_.note_level(relay_output(channel), state.test(channel) == kRelaySafeBit);
        }
    }
    last_ = state;
    return true;
}

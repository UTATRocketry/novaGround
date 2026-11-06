#include "stats.hpp"

#include <algorithm>
#include <boost/json.hpp>
#include <cctype>
#include <cmath>

namespace json = boost::json;
using namespace std::chrono;

namespace {
double to_ms(steady_clock::duration d) {
    return duration<double, std::milli>(d).count();
}

// Nearest-rank percentile of a sorted, non-empty vector.
double percentile(const std::vector<double>& sorted, double p) {
    auto rank = static_cast<std::size_t>(std::ceil(p / 100.0 * static_cast<double>(sorted.size())));
    rank = std::clamp<std::size_t>(rank, 1, sorted.size());
    return sorted[rank - 1];
}

json::object summary_json(const Summary& s) {
    json::object o;
    o["count"] = s.count;
    o["min"] = s.min;
    o["p50"] = s.p50;
    o["p99"] = s.p99;
    o["max"] = s.max;
    return o;
}
}  // namespace

bool parse_stats_setting(const char* value, bool& enabled) {
    enabled = false;
    if (!value) {
        return true;
    }
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (text.empty() || text == "0" || text == "false" || text == "off" || text == "no") {
        return true;
    }
    if (text == "1" || text == "true" || text == "on" || text == "yes") {
        enabled = true;
        return true;
    }
    return false;
}

Summary summarize(std::vector<double> values) {
    Summary s;
    s.count = values.size();
    if (values.empty()) {
        return s;
    }
    std::sort(values.begin(), values.end());
    s.min = values.front();
    s.max = values.back();
    s.p50 = percentile(values, 50.0);
    s.p99 = percentile(values, 99.0);
    return s;
}

void Window::add(double value) {
    if (values_.size() < kMaxWindow) {
        values_.push_back(value);
    } else {
        ++dropped_;
    }
}

Summary Window::take() {
    Summary s = summarize(std::move(values_));
    s.count += dropped_;
    values_.clear();
    dropped_ = 0;
    return s;
}

LoopStats::LoopStats(double configured_ms) : configured_ms_(configured_ms) {}

void LoopStats::tick(steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++ticks_;
    if (started_) {
        double period = to_ms(now - last_);
        periods_.add(period);
        if (period > configured_ms_ * kOverrunFactor) {
            ++overruns_;
        }
    }
    started_ = true;
    last_ = now;
}

LoopStats::Snapshot LoopStats::take() {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot s;
    s.configured_ms = configured_ms_;
    s.period_ms = periods_.take();
    s.ticks = ticks_;
    s.overruns = overruns_;
    return s;
}

StatsRegistry::StatsRegistry(StatsIdentity identity, bool enabled)
    : identity_(std::move(identity)), enabled_(enabled), started_(steady_clock::now()) {}

std::uint64_t StatsRegistry::next_telemetry_seq() {
    std::lock_guard<std::mutex> lock(seq_mutex_);
    return ++telemetry_seq_;
}

LoopStats* StatsRegistry::loop(const std::string& name, double configured_ms) {
    if (!enabled_) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto& slot = loops_[name];
    if (!slot) {
        slot = std::make_unique<LoopStats>(configured_ms);
    }
    return slot.get();
}

void StatsRegistry::record_command(const std::string& type,
                                   double received_to_handled_ms,
                                   double received_to_done_ms) {
    if (!enabled_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    CommandStats& c = commands_[type];
    ++c.count;
    c.to_handled_ms.add(received_to_handled_ms);
    c.to_done_ms.add(received_to_done_ms);
}

void StatsRegistry::count_publish_failure() {
    if (!enabled_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ++publish_failures_;
}

void StatsRegistry::count_daq_read_errors(std::uint64_t n) {
    if (!enabled_ || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    daq_read_errors_ += n;
}

std::string StatsRegistry::take_stats_json() {
    std::lock_guard<std::mutex> lock(mutex_);

    json::object root;
    root["node"] = identity_.node;
    root["env"] = identity_.env;
    root["build_id"] = identity_.build_id;
    root["git_commit"] = identity_.git_commit;
    root["actuation"] = identity_.actuation;
    root["seq"] = ++stats_seq_;
    root["time_ms"] = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    root["uptime_s"] = duration<double>(steady_clock::now() - started_).count();

    json::object loops;
    for (auto& [name, stats] : loops_) {
        LoopStats::Snapshot snap = stats->take();
        json::object o;
        o["configured_ms"] = snap.configured_ms;
        o["period_ms"] = summary_json(snap.period_ms);
        o["ticks"] = snap.ticks;
        o["overruns"] = snap.overruns;
        loops[name] = std::move(o);
    }
    root["loops"] = std::move(loops);

    json::object commands;
    for (auto& [type, c] : commands_) {
        json::object o;
        o["count"] = c.count;
        o["received_to_handled_ms"] = summary_json(c.to_handled_ms.take());
        o["received_to_done_ms"] = summary_json(c.to_done_ms.take());
        commands[type] = std::move(o);
    }
    root["commands"] = std::move(commands);

    root["publish_failures"] = publish_failures_;
    root["daq_read_errors"] = daq_read_errors_;
    return json::serialize(root);
}

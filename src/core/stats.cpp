#include "stats.hpp"

#include <algorithm>
#include <boost/json.hpp>
#include <cctype>
#include <cmath>

namespace json = boost::json;
using namespace std::chrono;

namespace {
constexpr int kSchema = 1;

double to_ms(steady_clock::duration d) {
    return duration<double, std::milli>(d).count();
}

std::int64_t system_ms() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
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

json::object report_json(const Distribution::Report& r) {
    json::object o;
    o["last"] = summary_json(r.last);
    o["total"] = summary_json(r.total);
    return o;
}

int bucket_of(double value) {
    if (value <= Histogram::kLowestMs) {
        return 0;
    }
    int index = static_cast<int>(std::floor(std::log2(value / Histogram::kLowestMs) * Histogram::kBucketsPerOctave));
    return std::clamp(index, 0, Histogram::kBuckets - 1);
}

// The top of a bucket: the value a percentile read from it reports.
double bucket_upper(int index) {
    return Histogram::kLowestMs * std::exp2(static_cast<double>(index + 1) / Histogram::kBucketsPerOctave);
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

bool parse_stats_interval(const char* value, int& interval_ms) {
    if (!value || !*value) {
        return true;
    }
    std::string text(value);
    if (text.size() > 9 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }
    int parsed = std::stoi(text);
    if (parsed < 1) {
        return false;
    }
    interval_ms = parsed;
    return true;
}

void Histogram::add(double value) {
    if (!(value >= 0.0) || std::isinf(value)) {  // NaN, negative, infinite
        return;
    }
    if (count_ == 0) {
        min_ = max_ = value;
    } else {
        min_ = std::min(min_, value);
        max_ = std::max(max_, value);
    }
    ++counts_[bucket_of(value)];
    ++count_;
}

Summary Histogram::summarize() const {
    Summary s;
    s.count = count_;
    if (count_ == 0) {
        return s;
    }
    s.min = min_;
    s.max = max_;

    // Nearest-rank: the smallest bucket whose running count reaches the rank.
    auto at = [this](double p) {
        auto rank = static_cast<std::uint64_t>(std::ceil(p / 100.0 * static_cast<double>(count_)));
        rank = std::clamp<std::uint64_t>(rank, 1, count_);
        std::uint64_t seen = 0;
        for (int i = 0; i < kBuckets; ++i) {
            seen += counts_[i];
            if (seen >= rank) {
                return std::clamp(bucket_upper(i), min_, max_);
            }
        }
        return max_;
    };
    s.p50 = at(50.0);
    s.p99 = at(99.0);
    return s;
}

void Histogram::clear() {
    counts_.fill(0);
    count_ = 0;
    min_ = max_ = 0.0;
}

void Distribution::add(double value) {
    last_.add(value);
    total_.add(value);
}

Distribution::Report Distribution::take() {
    Report r{last_.summarize(), total_.summarize()};
    last_.clear();
    return r;
}

LoopStats::LoopStats(double configured_ms) : configured_ms_(configured_ms) {}

void LoopStats::tick(steady_clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++ticks_;
    if (started_) {
        double period = to_ms(now - last_);
        periods_.add(period);
        if (period >= configured_ms_ * kOverrunFactor) {
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
    : identity_(std::move(identity)),
      enabled_(enabled),
      started_(steady_clock::now()),
      started_ms_(system_ms()) {}

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

void StatsRegistry::count_actuator_read_errors(std::uint64_t n) {
    if (!enabled_ || n == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    actuator_read_errors_ += n;
}

std::string StatsRegistry::identity_json() {
    json::object root;
    root["schema"] = kSchema;
    root["build_id"] = identity_.build_id;
    root["started_ms"] = started_ms_;
    root["stats_interval_ms"] = identity_.interval_ms;

    json::object loops;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [name, stats] : loops_) {
            json::object o;
            o["configured_ms"] = stats->configured_ms();
            loops[name] = std::move(o);
        }
    }
    root["loops"] = std::move(loops);

    // How to read the measurements in the stats message.
    json::object how;
    how["overrun_period_factor"] = LoopStats::kOverrunFactor;
    how["buckets_per_octave"] = Histogram::kBucketsPerOctave;
    how["lowest_ms"] = Histogram::kLowestMs;
    how["relative_error"] = Histogram::kRelativeError;
    root["measurement"] = std::move(how);
    return json::serialize(root);
}

std::string StatsRegistry::take_stats_json() {
    std::lock_guard<std::mutex> lock(mutex_);

    json::object root;
    root["seq"] = ++stats_seq_;
    root["time_ms"] = system_ms();
    root["uptime_s"] = duration<double>(steady_clock::now() - started_).count();

    json::object loops;
    for (auto& [name, stats] : loops_) {
        LoopStats::Snapshot snap = stats->take();
        json::object o;
        o["ticks"] = snap.ticks;
        o["overruns"] = snap.overruns;
        o["period_ms"] = report_json(snap.period_ms);
        loops[name] = std::move(o);
    }
    root["loops"] = std::move(loops);

    json::object commands;
    for (auto& [type, c] : commands_) {
        json::object o;
        o["count"] = c.count;
        o["received_to_handled_ms"] = report_json(c.to_handled_ms.take());
        o["received_to_done_ms"] = report_json(c.to_done_ms.take());
        commands[type] = std::move(o);
    }
    root["commands"] = std::move(commands);

    root["publish_failures"] = publish_failures_;
    root["daq_read_errors"] = daq_read_errors_;
    root["actuator_read_errors"] = actuator_read_errors_;
    return json::serialize(root);
}

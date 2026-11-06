#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>

// Runtime instrumentation; Everything here is hardware-free, and
// costs nothing when it is off: a disabled StatsRegistry hands out no loop
// statistics and ignores every record call.
//
// Two messages leave the process, both under nova/stats/<node>. The topic names
// the node, so neither payload repeats it:
//   nova/stats/<node>/info  retained; what this process is and how it measures
//                           (identity_json). Rarely changes, so a late
//                           subscriber needs it once.
//   nova/stats/<node>       not retained; one second (or NOVA_STATS_INTERVAL_MS)
//                           of measurements only (take_stats_json).
//
// Intervals are measured with steady_clock. Only timestamps that leave the
// process use system_clock.

// The NOVA_STATS setting. Unset or empty is off. "1", "true", "on" and "yes"
// are on; "0", "false", "off" and "no" are off (any letter case). Returns false
// for anything else and leaves `enabled` off: instrumentation must never stop
// the program, so the caller warns and carries on.
bool parse_stats_setting(const char* value, bool& enabled);

// The NOVA_STATS_INTERVAL_MS setting: a whole number of milliseconds, 1 or more.
// Unset or empty leaves `interval_ms` as it was (the caller's default) and
// returns true. Returns false for anything else (letters, fractions, zero,
// negative, trailing text, out of range), also leaving it unchanged.
bool parse_stats_interval(const char* value, int& interval_ms);

// min / p50 / p99 / max of a set of values. All zero when there are none. The
// percentiles come from a Histogram, so they are within kRelativeError of the
// true value; min and max are exact.
struct Summary {
    std::size_t count = 0;
    double min = 0.0;
    double p50 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};

// Fixed-size, log-spaced buckets (16 per octave, from 1 microsecond to about
// 4.5 minutes, in milliseconds). Constant memory and cost per value whatever the
// rate, and exact counts, so a slow loop's percentiles are as good as a fast
// one's. Values outside the range land in the first or last bucket (min and max
// still see them exactly). NaN and negative values are ignored.
class Histogram {
public:
    static constexpr int kBucketsPerOctave = 16;
    static constexpr int kOctaves = 28;
    static constexpr int kBuckets = kBucketsPerOctave * kOctaves;
    static constexpr double kLowestMs = 0.001;
    // One bucket is 2^(1/16) wide, so a percentile read from it is off by at
    // most this fraction of the true value.
    static constexpr double kRelativeError = 0.0443;

    void add(double value);
    Summary summarize() const;
    void clear();

private:
    std::array<std::uint64_t, kBuckets> counts_{};
    std::uint64_t count_ = 0;
    double min_ = 0.0;
    double max_ = 0.0;
};

// The same measurement over two spans: `last` since the previous take() (the
// last stats message), and `total` since the start. Short windows show what
// just happened; the total keeps slow loops' percentiles meaningful.
class Distribution {
public:
    struct Report {
        Summary last;
        Summary total;
    };

    void add(double value);
    Report take();  // reports both, then starts a new `last`

private:
    Histogram last_;
    Histogram total_;
};

// How often a loop actually ran, against how often it was asked to. The loops
// sleep for their interval after each pass, so a healthy pass takes slightly
// longer than the interval. A pass that took at least two intervals lost a
// whole tick, and counts as an overrun; that is the only threshold, and the
// period histogram is there for anyone who wants another.
class LoopStats {
public:
    static constexpr double kOverrunFactor = 2.0;

    struct Snapshot {
        double configured_ms = 0.0;
        Distribution::Report period_ms;
        std::uint64_t ticks = 0;     // since start
        std::uint64_t overruns = 0;  // since start
    };

    explicit LoopStats(double configured_ms);

    double configured_ms() const { return configured_ms_; }

    // Call once per pass of the loop. The first call only starts the clock.
    void tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    Snapshot take();

private:
    const double configured_ms_;
    std::mutex mutex_;
    bool started_ = false;
    std::chrono::steady_clock::time_point last_;
    Distribution periods_;
    std::uint64_t ticks_ = 0;
    std::uint64_t overruns_ = 0;
};

// What identifies this process. Published once, retained, in the info message.
// The node is the topic, not a field.
struct StatsIdentity {
    std::string node;       // the MQTT node id (novaGround, novaThermo, novaMock); names the topic
    std::string env;        // NOVA_ENV, reported only
    std::string build_id;
    std::string git_commit;
    std::string actuation = "enabled";  // enabled, disabled, or an only-actuators list
    int interval_ms = 1000;             // how often the stats message is published
};

class StatsRegistry {
public:
    StatsRegistry(StatsIdentity identity, bool enabled);

    bool enabled() const { return enabled_; }
    const StatsIdentity& identity() const { return identity_; }

    // Per-node telemetry packet counter, starting at 1. Thread-safe.
    std::uint64_t next_telemetry_seq();

    // The statistics for a named loop, created on first use (the first
    // configured_ms wins). Null when instrumentation is off. The pointer stays
    // valid for the registry's life. Register every loop before the info
    // message is published: it lists them with their configured periods.
    LoopStats* loop(const std::string& name, double configured_ms);

    // A command that reached its handler. `received_to_handled_ms` is receive ->
    // the handler about to run (parse and routing); `received_to_done_ms` is
    // receive -> the handler returned, which is after the driver write.
    void record_command(const std::string& type,
                        double received_to_handled_ms,
                        double received_to_done_ms);

    void count_publish_failure();
    void count_daq_read_errors(std::uint64_t n);

    // The retained info message, as JSON text.
    std::string identity_json();

    // One stats message as JSON text: measurements only. Starts new `last`
    // windows.
    std::string take_stats_json();

private:
    struct CommandStats {
        std::uint64_t count = 0;
        Distribution to_handled_ms;
        Distribution to_done_ms;
    };

    const StatsIdentity identity_;
    const bool enabled_;
    const std::chrono::steady_clock::time_point started_;
    const std::int64_t started_ms_;  // system clock, for the info message

    std::mutex mutex_;  // guards everything below
    std::map<std::string, std::unique_ptr<LoopStats>> loops_;
    std::map<std::string, CommandStats> commands_;
    std::uint64_t publish_failures_ = 0;
    std::uint64_t daq_read_errors_ = 0;
    std::uint64_t stats_seq_ = 0;

    std::mutex seq_mutex_;
    std::uint64_t telemetry_seq_ = 0;
};

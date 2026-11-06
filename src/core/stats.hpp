#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Runtime instrumentation; Everything here is hardware-free, and
// costs nothing when it is off: a disabled StatsRegistry hands out no loop
// statistics and ignores every record call.
//
// Intervals are measured with steady_clock. Only timestamps that leave the
// process (the telemetry fields, the stats message) use system_clock.

// The NOVA_STATS setting. Unset or empty is off. "1", "true", "on" and "yes"
// are on; "0", "false", "off" and "no" are off (any letter case). Returns false
// for anything else and leaves `enabled` off: instrumentation must never stop
// the program, so the caller warns and carries on.
bool parse_stats_setting(const char* value, bool& enabled);

// min / p50 / p99 / max of a set of values (nearest-rank percentiles). All zero
// when there are no values.
struct Summary {
    std::size_t count = 0;
    double min = 0.0;
    double p50 = 0.0;
    double p99 = 0.0;
    double max = 0.0;
};
Summary summarize(std::vector<double> values);

// A window of values that is summarised and emptied once per stats message. It
// keeps at most kMaxWindow values: past that it still counts, but stops storing.
class Window {
public:
    static constexpr std::size_t kMaxWindow = 10000;

    void add(double value);
    Summary take();  // summarises, then empties

private:
    std::vector<double> values_;
    std::size_t dropped_ = 0;
};

// How often a loop actually ran, against how often it was asked to. A period
// longer than kOverrunFactor times the configured one counts as an overrun.
class LoopStats {
public:
    static constexpr double kOverrunFactor = 1.5;

    struct Snapshot {
        double configured_ms = 0.0;
        Summary period_ms;      // since the last snapshot
        std::uint64_t ticks = 0;     // since start
        std::uint64_t overruns = 0;  // since start
    };

    explicit LoopStats(double configured_ms);

    // Call once per pass of the loop. The first call only starts the clock.
    void tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    Snapshot take();

private:
    const double configured_ms_;
    std::mutex mutex_;
    bool started_ = false;
    std::chrono::steady_clock::time_point last_;
    Window periods_;
    std::uint64_t ticks_ = 0;
    std::uint64_t overruns_ = 0;
};

// What identifies this process in every stats message.
struct StatsIdentity {
    std::string node;       // the MQTT node id (novaGround, novaThermo, novaMock)
    std::string env;        // NOVA_ENV, reported only
    std::string build_id;
    std::string git_commit;
    std::string actuation = "enabled";  // enabled, disabled, or an only-actuators list
};

class StatsRegistry {
public:
    StatsRegistry(StatsIdentity identity, bool enabled);

    bool enabled() const { return enabled_; }

    // Per-node telemetry packet counter, starting at 1. Thread-safe.
    std::uint64_t next_telemetry_seq();

    // The statistics for a named loop, created on first use. Null when
    // instrumentation is off. The pointer stays valid for the registry's life.
    LoopStats* loop(const std::string& name, double configured_ms);

    // A command that reached its handler. `received_to_handled_ms` is receive ->
    // the handler about to run (parse and routing); `received_to_done_ms` is
    // receive -> the handler returned, which is after the driver write.
    void record_command(const std::string& type,
                        double received_to_handled_ms,
                        double received_to_done_ms);

    void count_publish_failure();
    void count_daq_read_errors(std::uint64_t n);

    // One stats message as JSON text. Empties the per-message windows.
    std::string take_stats_json();

private:
    struct CommandStats {
        std::uint64_t count = 0;
        Window to_handled_ms;
        Window to_done_ms;
    };

    const StatsIdentity identity_;
    const bool enabled_;
    const std::chrono::steady_clock::time_point started_;

    std::mutex mutex_;  // guards everything below
    std::map<std::string, std::unique_ptr<LoopStats>> loops_;
    std::map<std::string, CommandStats> commands_;
    std::uint64_t publish_failures_ = 0;
    std::uint64_t daq_read_errors_ = 0;
    std::uint64_t stats_seq_ = 0;

    std::mutex seq_mutex_;
    std::uint64_t telemetry_seq_ = 0;
};

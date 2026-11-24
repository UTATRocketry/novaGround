#pragma once

#include <bitset>
#include <chrono>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/pi_io_config.hpp"
#include "interfaces/digital_io.hpp"
#include "interfaces/relay_bank.hpp"

class StatsRegistry;

// Output limits on the Pi's relay and GPIO outputs (docs/design/pi-io-config.md,
// "Output limits"): a maximum on-time, and what to do when the backend goes
// quiet. Hardware-free; the clocks are injected so tests can drive time.
//
//   GpioController / RelayController
//     -> SupervisedDigitalIo / SupervisedRelayBank   (record when each output
//                                                     leaves its safe level;
//                                                     refuse while stale)
//     -> StoppedDigitalIo / StoppedRelayBank         (only with --stop-actuate)
//     -> LinuxGpio / TCA9535
//
// The supervisor never writes an output itself. When tick() finds an output to
// return to its safe level it calls the trip handler, which goes through the
// output's controller (so the controller's own state, the telemetry and the
// actuator log stay right) and so through both wrappers: under --stop-actuate a
// trip moves nothing either.
//
// Safe means a GPIO level of 0 and a de-energized relay (relay bit 1; the relay
// boards are active-low), which is what startup sets.
//
// A trip latches: the output stays at its safe level, and is listed in
// tripped(), until the next command for it (command_applied()).

enum class OutputKind { Gpio, Relay };

struct OutputId {
    OutputKind kind = OutputKind::Gpio;
    int index = 0;  // BCM pin or relay channel

    auto operator<=>(const OutputId&) const = default;
    // "gpio:17", "relay:3": the form the config status uses.
    std::string name() const;
};

inline OutputId gpio_output(int pin) { return {OutputKind::Gpio, pin}; }
inline OutputId relay_output(int channel) { return {OutputKind::Relay, channel}; }

constexpr int kGpioSafeLevel = 0;
constexpr bool kRelaySafeBit = true;  // de-energized

struct Trip {
    OutputId output;
    std::string reason;    // "max_on_time" or "comms_loss"
    std::int64_t time_ms;  // system clock

    bool operator==(const Trip&) const = default;
};

class OutputSupervisor {
public:
    using SteadyClock = std::function<std::chrono::steady_clock::time_point()>;
    using SystemMs = std::function<std::int64_t()>;
    // Drives `output` to its safe level through its controller, which then calls
    // record_trip(). Called from tick(), without the supervisor's lock held.
    using TripHandler = std::function<void(const OutputId& output, const std::string& reason)>;

    // Empty clocks mean the real ones.
    explicit OutputSupervisor(SteadyClock steady = {}, SystemMs system_ms = {});

    void set_trip_handler(TripHandler handler);
    void set_stats(StatsRegistry* stats) { stats_ = stats; }

    // From the config. Replaces every output's limits (an output not listed has
    // none) and the heartbeat timeout. Comms-loss supervision runs only while a
    // timeout is set: it starts when the first config with a heartbeat section
    // arrives, with a full timeout's grace from then.
    void configure(std::map<OutputId, OutputLimits> limits, std::optional<std::int64_t> heartbeat_timeout_ms);

    // A message on nova/heartbeat/backend. A live {"online": true, ...} is a
    // beat and clears a comms loss. {"online": false} (the backend's last will)
    // is a comms loss, retained or not. A retained {"online": true} is neither:
    // it says only that the backend was up when it was published. Anything
    // else is ignored.
    void heartbeat(const std::string& payload, bool retained);
    // The MQTT connection dropped. A comms loss until the next live beat.
    void connection_lost();

    // From the wrappers. Whether a write that moves `output` to (or keeps it
    // at) its safe level, or off it, may happen now. Refused: off safe, while
    // the heartbeat is stale, for an output set to `safe` on comms loss.
    bool allow_write(const OutputId& output, bool to_safe);
    // A write went through: the output is now at (or off) its safe level. The
    // max on-time counts from when an output last left its safe level.
    void note_level(const OutputId& output, bool at_safe);
    // The output is no longer driven by this program (a released GPIO pin):
    // forget its level and any trip.
    void forget(const OutputId& output);

    // From the controllers. A command for `output` was carried out: its trip,
    // if any, is cleared.
    void command_applied(const OutputId& output);
    // The controller drove `output` to its safe level for a trip.
    void record_trip(const OutputId& output, const std::string& reason);

    // Checks every output against its limits and the heartbeat, and trips
    // those that must go back to their safe level. Call every 50 ms.
    void tick();

    bool comms_stale();
    std::vector<Trip> tripped();
    // True once after the trips changed (a trip, or a command clearing one).
    bool take_changed();

private:
    struct Level {
        bool at_safe = true;
        std::chrono::steady_clock::time_point left_safe{};
    };

    // Caller holds mutex_.
    bool stale_locked(std::chrono::steady_clock::time_point now) const;

    SteadyClock steady_;
    SystemMs system_ms_;
    TripHandler trip_handler_;
    StatsRegistry* stats_ = nullptr;

    std::mutex mutex_;
    std::map<OutputId, OutputLimits> limits_;
    std::map<OutputId, Level> levels_;
    std::map<OutputId, Trip> trips_;
    std::map<OutputId, std::chrono::steady_clock::time_point> trip_attempts_;
    std::optional<std::int64_t> heartbeat_timeout_ms_;
    std::chrono::steady_clock::time_point last_beat_{};
    bool comms_lost_ = false;
    bool was_stale_ = false;
    bool changed_ = false;
};

// GPIO writes through the supervisor: records each output's level and refuses a
// write that would move a `safe` output off its safe level while the heartbeat
// is stale. Reads, input claims and releases go straight through. `inner` and
// `supervisor` are not owned and must outlive the wrapper.
class SupervisedDigitalIo : public DigitalIo {
public:
    SupervisedDigitalIo(DigitalIo& inner, OutputSupervisor& supervisor, StatsRegistry* stats = nullptr)
        : inner_(inner), supervisor_(supervisor), stats_(stats) {}

    bool configure_output(int pin, int level) override;
    bool configure_input(int pin) override;
    bool release(int pin) override;
    bool write(int pin, int value) override;
    bool read(int pin, int& value) override { return inner_.read(pin, value); }
    std::vector<int> get_input_pins() override { return inner_.get_input_pins(); }
    std::map<int, int> read_all_inputs() override { return inner_.read_all_inputs(); }
    std::map<int, int> read_all_outputs() override { return inner_.read_all_outputs(); }

private:
    DigitalIo& inner_;
    OutputSupervisor& supervisor_;
    StatsRegistry* stats_;
};

// Relay writes through the supervisor. The relay bank is written 16 channels at
// a time, so the wrapper compares each write with the last one that went through
// to see which channels move. `initial` is what startup wrote.
class SupervisedRelayBank : public RelayBank {
public:
    SupervisedRelayBank(RelayBank& inner,
                        OutputSupervisor& supervisor,
                        const std::bitset<16>& initial,
                        StatsRegistry* stats = nullptr);

    bool is_ready() const override { return inner_.is_ready(); }
    bool write_output(const std::bitset<16>& state) override;
    bool read_output(std::bitset<16>& state) override { return inner_.read_output(state); }

private:
    RelayBank& inner_;
    OutputSupervisor& supervisor_;
    StatsRegistry* stats_;
    std::mutex mutex_;
    std::bitset<16> last_;
};

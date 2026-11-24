#pragma once

#include <bitset>
#include <boost/json.hpp>
#include <mutex>
#include <string>

#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/relay_bank.hpp"

class OutputSupervisor;

class RelayController {
public:
    RelayController(RelayBank* expander, TelemetryStore* telemetry, DataLogger* logger);

    // Not owned. Told about each command carried out, so it can clear a trip.
    void set_supervisor(OutputSupervisor* supervisor);

    // {"type":"relay","id":<0-15>,"state":<bool or 0/1>}. The state the
    // controller keeps changes only when the write went through, so a failed
    // or refused write is not carried into the next command's write.
    void handle_command(const boost::json::object& cmd);
    void set_initial_state(const std::bitset<16>& state);

    // De-energizes one channel (bit 1) for the output supervisor and records the
    // trip.
    void drive_safe(int channel, const std::string& reason);

private:
    bool available() const;
    // Caller holds mutex_. Writes `next`; on success it becomes the state.
    bool write_locked(const std::bitset<16>& next, const char* type_id);

    RelayBank* expander_ = nullptr;
    TelemetryStore* telemetry_ = nullptr;
    DataLogger* logger_ = nullptr;
    OutputSupervisor* supervisor_ = nullptr;
    std::bitset<16> relay_state_;
    std::mutex mutex_;
};

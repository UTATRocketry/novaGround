#pragma once

#include <optional>
#include <string>
#include <vector>

// One DAQ HAT found on the bus. `channels` is filled in by the driver that
// discovered it (empty for a HAT type the driver does not implement).
struct DaqHatDevice {
    int address = 0;
    int hat_id = 0;
    std::vector<int> channels;
    // An MCC 134: its channels are thermocouples, with a type (set_tc_type).
    bool thermocouples = false;
};

// The MCC 134's thermocouple types. `Disabled` is the library's TC_DISABLED:
// the config can't ask for it, but an unconfigured channel could be set to it
// (see kUnconfiguredTcType).
enum class TcType { J, K, T, E, R, S, B, N, Disabled };

// The type an MCC 134 channel is opened with, and the one a channel with no
// configured sensor is set to: K, which is what every channel had before types
// were configurable. Open question (docs/design/pi-io-config.md in
// Nova-Collected): TcType::Disabled instead. Changing this one line is all it
// takes.
constexpr TcType kUnconfiguredTcType = TcType::K;

// "J", "K", ... and "disabled".
const char* tc_type_name(TcType type);
// A config letter: exactly one of J K T E R S B N. nullopt otherwise.
std::optional<TcType> parse_tc_type(const std::string& text);

// What the sampler needs from the analog hardware. Named AnalogIo, not
// AnalogInput, in case outputs are ever added.
class AnalogIo {
public:
    virtual ~AnalogIo() = default;

    // Throws std::runtime_error if the bus cannot be listed.
    virtual std::vector<DaqHatDevice> discover() = 0;
    // Opens a HAT. An MCC 134's channels are set to kUnconfiguredTcType.
    virtual bool open(const DaqHatDevice& hat) = 0;
    virtual bool read(const DaqHatDevice& hat, int channel, double& value) = 0;

    // Sets an MCC 134 channel's thermocouple type. False (logged) if the HAT at
    // `address` is not an open MCC 134 or the library refused. The default is
    // for drivers without thermocouples.
    virtual bool set_tc_type(int /*address*/, int /*channel*/, TcType /*type*/) { return false; }
};

// Find the HATs on the bus and open each; returns the ones that opened. A
// failure to list the bus is logged and gives an empty list.
std::vector<DaqHatDevice> discover_and_open(AnalogIo& daq);

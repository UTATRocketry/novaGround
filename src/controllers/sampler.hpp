#pragma once

#include <stop_token>
#include <string>
#include <vector>

#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/analog_io.hpp"
#include "interfaces/digital_io.hpp"

// Samplers: poll hardware and write the result into the TelemetryStore. Each
// has a *_once function (one pass, testable) and a *_loop that repeats it.

// Find the HATs on the bus and open each; returns the ones that opened. A
// failure to list the bus is logged and gives an empty list.
std::vector<DaqHatDevice> discover_and_open(AnalogIo& daq);

// "timestamp", then hat<address>_ch<channel> for every channel of every HAT
// that has channels. Matches the order daq_sample_once logs values in.
std::vector<std::string> build_sensor_headers(const std::vector<DaqHatDevice>& hats);

// One pass over every channel: read it, store the samples, and log a row if
// the logger is recording. A failed read is NaN.
void daq_sample_once(AnalogIo& daq,
                     const std::vector<DaqHatDevice>& daq_hats,
                     TelemetryStore& store,
                     DataLogger* logger);

// daq_sample_once every sample_interval_ms until a stop is requested. As the
// first parameter the token is passed automatically by std::jthread.
void daq_sample_loop(std::stop_token stop,
                     AnalogIo& daq,
                     const std::vector<DaqHatDevice>& daq_hats,
                     TelemetryStore& store,
                     DataLogger* logger,
                     int sample_interval_ms);

// One pass: read every input pin and replace the stored GPIO states. An
// exception from the hardware is logged and swallowed, so the loop survives.
void gpio_sample_once(DigitalIo& gpio, TelemetryStore& store);

// gpio_sample_once every interval_ms until a stop is requested. (No default
// for the interval: std::jthread cannot use default arguments.)
void gpio_sample_loop(std::stop_token stop, DigitalIo& gpio, TelemetryStore& store, int interval_ms);

#pragma once

#include <string>
#include <vector>

#include "core/data_logger.hpp"
#include "core/telemetry.hpp"
#include "interfaces/analog_io.hpp"
#include "interfaces/digital_io.hpp"

// Samplers: poll hardware and write the result into the TelemetryStore. Each
// has a *_once function (one pass, testable) and a *_loop that repeats it.

// "timestamp", then hat<address>_ch<channel> for every channel of every HAT
// that has channels. Matches the order daq_sample_once logs values in.
std::vector<std::string> build_sensor_headers(const std::vector<DaqHatDevice>& hats);

// One pass over every channel: read it, store the samples, and log a row if
// the logger is recording. A failed read is NaN.
void daq_sample_once(AnalogIo& daq,
                     const std::vector<DaqHatDevice>& daq_hats,
                     TelemetryStore& store,
                     DataLogger* logger);

// daq_sample_once every sample_interval_ms, forever.
void daq_sample_loop(AnalogIo& daq,
                     const std::vector<DaqHatDevice>& daq_hats,
                     TelemetryStore& store,
                     DataLogger* logger,
                     int sample_interval_ms);

// One pass: read every input pin and replace the stored GPIO states. An
// exception from the hardware is logged and swallowed, so the loop survives.
void gpio_sample_once(DigitalIo& gpio, TelemetryStore& store);

// gpio_sample_once every interval_ms, forever. (No default for the interval:
// std::thread cannot use default arguments.)
void gpio_sample_loop(DigitalIo& gpio, TelemetryStore& store, int interval_ms);

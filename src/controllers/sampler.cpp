#include "sampler.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

#include "utils/interruptible_sleep.hpp"

std::vector<std::string> build_sensor_headers(const std::vector<DaqHatDevice>& hats) {
    std::vector<std::string> headers;
    headers.push_back("timestamp");
    for (const auto& hat : hats) {
        if (hat.channels.empty()) {
            continue;
        }
        std::string prefix = "hat" + std::to_string(hat.address);
        for (int channel : hat.channels) {
            headers.push_back(prefix + "_ch" + std::to_string(channel));
        }
    }
    return headers;
}

void daq_sample_once(AnalogIo& daq,
                     const std::vector<DaqHatDevice>& daq_hats,
                     TelemetryStore& store,
                     DataLogger* logger) {
    std::vector<SensorSample> new_data;
    std::vector<double> values;
    size_t total_channels = 0;
    for (const auto& hat : daq_hats) {
        total_channels += hat.channels.size();
    }
    new_data.reserve(total_channels);
    values.reserve(total_channels);

    auto now = std::chrono::system_clock::now();
    double timestamp_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count();

    for (const auto& hat : daq_hats) {
        for (int channel : hat.channels) {
            double value = std::numeric_limits<double>::quiet_NaN();
            bool ok = daq.read(hat, channel, value);

            SensorSample sd;
            sd.hat_id = hat.address;
            sd.channel_id = channel;
            sd.value = ok ? value : std::numeric_limits<double>::quiet_NaN();
            sd.timestamp_ms = timestamp_ms;
            new_data.push_back(sd);
            values.push_back(sd.value);
        }
    }

    store.set_sensors(std::move(new_data));
    if (logger && logger->is_active()) {
        logger->log_sensor_row(timestamp_ms, values);
    }
}

void daq_sample_loop(std::stop_token stop,
                     AnalogIo& daq,
                     const std::vector<DaqHatDevice>& daq_hats,
                     TelemetryStore& store,
                     DataLogger* logger,
                     int sample_interval_ms) {
    int interval_ms = sample_interval_ms > 0 ? sample_interval_ms : 1;
    InterruptibleSleep sleeper;
    while (!stop.stop_requested()) {
        daq_sample_once(daq, daq_hats, store, logger);
        if (sleeper.sleep_for(stop, std::chrono::milliseconds(interval_ms))) {
            break;
        }
    }
}

void gpio_sample_once(DigitalIo& gpio, TelemetryStore& store) {
    try {
        auto input_vals = gpio.read_all_inputs();
        store.set_gpio_states(std::move(input_vals));
    } catch (const std::exception& e) {
        std::cerr << "GPIO sampler error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "GPIO sampler error: unknown exception" << std::endl;
    }
}

void gpio_sample_loop(std::stop_token stop, DigitalIo& gpio, TelemetryStore& store, int interval_ms) {
    InterruptibleSleep sleeper;
    while (!stop.stop_requested()) {
        gpio_sample_once(gpio, store);
        if (sleeper.sleep_for(stop, std::chrono::milliseconds(interval_ms))) {
            break;
        }
    }
}

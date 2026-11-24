#pragma once

#include <gpiod.h>

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "interfaces/digital_io.hpp"

// DigitalIo on a Linux GPIO chip through libgpiod (the v1 API: 1.6 or later,
// before 2.0; meson.build checks). Each claim is one line request. Every failed
// request, write or read is logged with the pin and the error libgpiod gave
// (errno), and returns false.
class LinuxGpio : public DigitalIo {
public:
    // Throws std::runtime_error if the chip cannot be opened.
    explicit LinuxGpio(const std::string& chipname = "/dev/gpiochip0");
    ~LinuxGpio() override;

    LinuxGpio(const LinuxGpio&) = delete;
    LinuxGpio& operator=(const LinuxGpio&) = delete;

    bool configure_output(int pin, int level) override;
    bool configure_input(int pin) override;
    bool release(int pin) override;
    bool write(int pin, int value) override;
    bool read(int pin, int& value) override;
    std::vector<int> get_input_pins() override;
    std::map<int, int> read_all_inputs() override;
    std::map<int, int> read_all_outputs() override;

private:
    enum class Direction { In, Out };
    struct PinInfo {
        gpiod_line* line = nullptr;
        Direction direction = Direction::In;
    };

    // Caller holds mutex_. Releases the pin's request, if it has one.
    void release_locked(int pin);
    // Caller holds mutex_. The line for `pin`, or null (logged).
    gpiod_line* line_for(int pin);

    gpiod_chip* chip_ = nullptr;
    std::map<int, PinInfo> pins_;
    std::mutex mutex_;
};

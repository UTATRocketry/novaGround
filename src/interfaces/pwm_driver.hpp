#pragma once

#include <cstdint>

// What a channel is doing: held low, held high, or pulsing.
enum class PwmOutput { Off, On, Pulse };

// A channel as the hardware holds it. `microseconds` is the pulse width and is
// only meaningful for Pulse (0 otherwise). It is derived from the chip's tick
// counts, so it can differ from the width that was written by up to one tick
// (about 5 us at 50 Hz).
struct PwmChannelReadback {
    PwmOutput output = PwmOutput::Off;
    uint16_t microseconds = 0;
};

// What ServoController needs from the PWM hardware. The method names are the
// ones Adafruit_PWMServoDriver already had.
class PwmDriver {
public:
    virtual ~PwmDriver() = default;

    virtual bool is_ready() const = 0;
    virtual void writeMicroseconds(uint8_t channel, uint16_t microseconds) = 0;
    virtual bool disablePWM(uint8_t channel) = 0;

    // Reads back what the hardware holds for `channel` (0 to 15). Changes
    // nothing. False if the channel is out of range or the read failed.
    virtual bool read_channel(uint8_t channel, PwmChannelReadback& readback) = 0;
};

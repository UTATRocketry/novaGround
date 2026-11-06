#pragma once

#include <cstdint>

// What ServoController needs from the PWM hardware. The method names are the
// ones Adafruit_PWMServoDriver already had.
class PwmDriver {
public:
    virtual ~PwmDriver() = default;

    virtual bool is_ready() const = 0;
    virtual void writeMicroseconds(uint8_t channel, uint16_t microseconds) = 0;
    virtual bool disablePWM(uint8_t channel) = 0;
};

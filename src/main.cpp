#include "../dependencies/Adafruit_PWMServoDriver.h"
#include "interfaces/ActuatorInterface/ActuatorManager/DummyActuatorManager.h"
#include "interfaces/SensorInterface/SensorManager/DummySensorManager.h"

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

auto main() -> int {

#ifdef WIRINGPI_AVAILABLE
    Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

    pwm.begin();
    pwm.setPWMFreq(60.0);

    while (true) {
        for (uint16_t i = 0; i < 4096; i += 8) {
            for (uint8_t pwmnum = 0; pwmnum < 16; pwmnum++) {
                pwm.setPWM(pwmnum, 0, (i + (4096 / 16) * pwmnum) % 4096);
            }
        }
    }
#endif
}
#include "../dependencies/Adafruit_PWMServoDriver.h"
#include "interfaces/ActuatorInterface/ActuatorManager/DummyActuatorManager.h"
#include "interfaces/SensorInterface/SensorManager/DummySensorManager.h"

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

auto main() -> int {

    Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

    pwm.setPWMFreq(60.0);

    while (true) {
        pwm.setPWM(0, 0, 370);
        usleep(1'000'000);
        pwm.setPWM(0, 0, 415);
        usleep(1'000'000);
        pwm.setPWM(0, 0, 460);
        usleep(1'000'000);
        pwm.setPWM(0, 0, 415);
        usleep(1'000'000);
    }
}
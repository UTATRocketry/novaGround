#include "entities/control/Actuator/DummyActuator.h"
#include "entities/control/Actuator/RelayActuator.h"
#include "entities/dataAcquisition/Sensor/DummySensor.h"
#include <iostream>
#include <unistd.h>
#include <pigpio.h>


auto main() -> int {
    if (gpioInitialise()< 0) {
        std::cout << "failed";
        return 1;
    }
    gpioSetMode(23, PI_OUTPUT);

    auto* rel_18 = new RelayActuator(0, 18,);
    auto* rel_23 = new RelayActuator(0, 23);
    auto* rel_24 = new RelayActuator(0, 24);
    auto* rel_25 = new RelayActuator(0, 25);
    auto* rel_8 = new RelayActuator(0, 8);
    auto* relay_actuator = new RelayActuator(0, 12);
    auto* relay_actuator = new RelayActuator(0, 16);
    auto* relay_actuator = new RelayActuator(0, 20);



    auto* sensor = new DummySensor(1);
    auto* actuator = new DummyActuator(2);

    int i{0};

    relay_actuator->setActuatorState(true);

    while (i++ < 100) {
        // std::cout << "ACTUATION" << i%2 == 0 << '\n';s
        // actuator->setActuatorState(i % 2 == 0);
        relay_actuator->setActuatorState((bool)(i%2 ==0));
        // std::cout << sensor->readData() << "\n";
        sleep(1);
    };

    relay_actuator->setActuatorState(false);

    delete relay_actuator;
    delete sensor;
    delete actuator;

    gpioTerminate();
}

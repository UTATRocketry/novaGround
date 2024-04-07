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

    auto* rel_18 = new RelayActuator(0, 18);
    auto* rel_23 = new RelayActuator(0, 23);
    auto* rel_24 = new RelayActuator(0, 24);
    auto* rel_25 = new RelayActuator(0, 25);
    auto* rel_8 = new RelayActuator(0, 8);
    auto* rel_12 = new RelayActuator(0, 12);
    auto* rel_16 = new RelayActuator(0, 16);
    auto* rel_20 = new RelayActuator(0, 20);

    auto* sensor = new DummySensor(1);
    auto* actuator = new DummyActuator(2);

    int i {0};
    while (++i < 100){
        rel_23->setActuatorState((bool)(i%2)); 
        sleep(1);
    }


    delete rel_18;
    delete rel_23;
    delete rel_24;
    delete rel_25;
    delete rel_8;
    delete rel_12 ;
    delete rel_16;
    delete rel_20;
    delete sensor;
    delete actuator;

    gpioTerminate();
}

#include "entities/control/Actuator/DummyActuator.h"
#include "entities/dataAcquisition/Sensor/DummySensor.h"
#include <iostream>

auto main() -> int {
    std::cout << "TEST12356"
              << "\n";

    auto* sensor = new DummySensor(1);
    auto* actuator = new DummyActuator(2);

    int i{0};

    while (i++ < 100) {
        actuator->setActuatorState(i % 2 == 0);
        std::cout << sensor->readData() << "\n";
    };

    delete sensor;
    delete actuator;
}

#include "entities/dataAcquisition/Sensor/DummySensor.h"
#include <iostream>

auto main() -> int {
    std::cout << "TEST12356"
              << "\n";
    auto* test = new DummySensor();

    std::cout << test->readData() << "\n";
    delete test;
}

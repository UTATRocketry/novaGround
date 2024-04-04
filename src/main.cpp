#include <iostream>
#include "entities/dataAcquisition/Sensor/DummySensor.h"

auto main() -> int{
	std::cout << "TEST123" << "\n";
	auto *test = new DummySensor();

	std::cout << test->readData() << "\n";
	delete test;
}

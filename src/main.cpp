#include <chrono>
#include <thread>
#include <iostream>
#include "entities/dataAcquisition/Sensor/DummySensor.h"

int main(){
	std::cout << "TEST123" << "\n";
	DummySensor *test = new DummySensor();

	std::cout << test->readData() << "\n";
	delete test;
}

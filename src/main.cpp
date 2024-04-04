#include <chrono>
#include <thread>
#include <iostream>
#include "entities/dataAcquisition/Sensor/DummySensor.h"

int main(){
	std::cout << "TEST123" << std::endl;
	dummy_Sensor *test = new dummy_Sensor();

	std::cout << test->readData() << std::endl;
	delete test;
}

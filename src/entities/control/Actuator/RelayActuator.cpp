#include "RelayActuator.h"

RelayActuator::RelayActuator(int id, int gpio_pin, bool initial_state) : Actuator(id) {
	this->id = id; 
	this->gpio_pin_ = gpio_pin;
	this->actuator_state_ = initial_state;

	gpioSetMode(this->gpio_pin_, PI_OUTPUT);
	gpioWrite(this->gpio_pin_, initial_state);
}

auto RelayActuator::setActuatorState(bool state) -> int {
	std::cout << state << '\n'; 
	gpioWrite(this->gpio_pin_, state);
	this->actuator_state_ = state;
	return 0;
}

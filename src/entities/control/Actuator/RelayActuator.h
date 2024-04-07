#pragma once

#include "Actuator.h"
#include <iostream>
#include <pigpio.h>

class RelayActuator : public Actuator<bool>{
	public:
		RelayActuator(int id, int gpio_pin, bool initial_state=true);
	
		auto setActuatorState(bool state) -> int;

	private:
			int gpio_pin_; 
};

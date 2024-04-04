#include "DummyActuator.h"
#include <iostream>

auto DummyActuator::setActuatorState(bool state) -> int {
    this->actuator_state_ = state;
    std::cout << "Actuation Event " << state << "\n";
    return 0;
};
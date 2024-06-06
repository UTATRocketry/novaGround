#include "DummyActuator.h"
#include <iostream>

auto DummyActuator::setState(bool state) -> int {
    this->state_ = state;
    std::cout << "Actuation Event " << state << "\n";
    return 0; // indicate success
};

auto DummyActuator::getState() -> bool { return this->state_; }
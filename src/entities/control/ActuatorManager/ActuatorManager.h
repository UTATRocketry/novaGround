#pragma once

#include "../Actuator/Actuator.h"
#include <memory>
#include <vector>

template <typename StateType>
class ActuatorManager {
  public:
    virtual auto setActuatorValue(int id, StateType state) -> double;

    virtual auto getNumBoolActuators() -> int;

    // TODO might remove
    // Get array of non owning pointers for sensors
    // (keeps ownership in SensorManager)
    virtual auto getActuators() -> std::vector<Actuator<StateType>*>;

  private:
    std::vector<std::unique_ptr<Actuator<bool>>> bool_actuators_;
};
#include "Actuator.h"

class DummyActuator : private Actuator<bool> {
  public:
    DummyActuator(int id) : Actuator(id) {};
    auto setActuatorState(bool state) -> int override;

    int id;
};
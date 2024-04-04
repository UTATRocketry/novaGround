#include "BoolActuator.h"

class DummyActuator : private BoolActuator {
  public:
    DummyActuator(int id) : BoolActuator(int id) {};
    auto setActuatorState(bool state) -> int override;

    int id;
};
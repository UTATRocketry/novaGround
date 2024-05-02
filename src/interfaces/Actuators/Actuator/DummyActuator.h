#include "Actuator.h"

class DummyActuator : private Actuator<bool> {
  public:
    explicit DummyActuator(int id) : Actuator(id){};
    auto setActuatorState(bool state) -> int override;

    int id;
};
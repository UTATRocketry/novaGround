#include "Actuator.h"
#include <string>
#include <utility>

class DummyActuator : public Actuator<bool> {
  public:
    DummyActuator(int id, std::string name, bool defaultState)
        : Actuator<bool>(id, std::move(name), defaultState){};
    auto getState() -> bool override;
    auto setState(bool state) -> int override;
};
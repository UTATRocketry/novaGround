#include "../Actuator/DummyActuator.h"
#include "ActuatorManager.h"
#include <memory>
#include <string>
#include <tuple>
#include <vector>

#pragma once

class DummyActuatorManager : public ActuatorManager<bool> {
  public:
    explicit DummyActuatorManager(
        const std::vector<std::tuple<int, std::string, bool>>& params) {
        for (const std::tuple<int, std::string, bool>& actuator : params) {
            this->actuators_[std::get<0>(actuator)] =
                std::make_unique<DummyActuator>(std::get<0>(actuator),
                                                std::get<1>(actuator),
                                                std::get<2>(actuator));
        }
    };
};
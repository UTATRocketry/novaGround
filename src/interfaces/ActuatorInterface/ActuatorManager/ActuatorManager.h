#pragma once

#include "../Actuator/Actuator.h"
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

template <typename StateType> class ActuatorManager {
  public:
    ActuatorManager() {
        this->actuators_ =
            std::map<int, std::unique_ptr<Actuator<StateType>>>();
    }
    /**
     * Set actuator state by id
     *
     * TODO - Add error handling for sensor not found
     *
     * @param id which actuator to change state of
     * @param state new state of actuator
     *
     * @return int (0 if state change was sucessful, -1 otherwise)
     */
    auto setState(int id, StateType state) -> int {
        return this->actuators_.at(id)->setState(state);
    };

    /**
     * Get actuator state by id
     *
     * TODO - Add error handling for sensor not found
     *
     * @param id which actuator to check state of
     * @returns State of actuator (id)
     */
    auto getState(int id) -> StateType {
        return this->actuators_.at(id)->getState();
    };

    /**
     * Get summary of actuators used for setting up UI
     *
     * @returns Vector containing id, name, state tuple
     */
    auto getActuatorSummary()
        -> std::vector<std::tuple<int, std::string, StateType>> {

        std::vector<std::tuple<int, std::string, StateType>> summaries;

        for (auto& actuator : this->actuators_) {
            std::tuple<int, std::string, StateType> summary{
                actuator.second->id, actuator.second->name,
                actuator.second->getState()};

            summaries.push_back(summary);
        }

        return summaries;
    };

  protected:
    std::map<int, std::unique_ptr<Actuator<StateType>>> actuators_;
};
#pragma once

#include "../Actuator/Actuator.h"
#include <memory>
#include <vector>

template <typename StateType>
class ActuatorManager {
  public:

    /**
     * Set actuator state by id
     *
     * @param id which actuator to change state of
     * @param state new state of actuator
    */
    virtual auto setActuatorState(int id, StateType state) -> bool = 0;

    /**
     * Get actuator state by id
     *
     * @param id which actuator to check state of
    */
    virtual auto getActuatorState(int id) -> StateType = 0;


    /**
    * TODO maybe remove??
    * Gets non owning pointers to all of the actuators
    * Ideally all actuators should be managed through
    * the respective managers
    *
    * @return std::vector<Actuator<StateType>*> Vector of non owning pointers
    **/
      virtual auto getActuators() -> std::vector<Actuator<StateType>*> = 0;

  private:
    std::vector<std::unique_ptr<Actuator<StateType>>> actuators_;
};
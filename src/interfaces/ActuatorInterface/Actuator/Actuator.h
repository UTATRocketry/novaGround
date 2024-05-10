#pragma once
#include <string>
#include <utility>

template <typename StateType> class Actuator {
  public:
    explicit Actuator(int id, std::string name, StateType defaultState)
        : id(id), name(std::move(std::move(name))), state_(defaultState){};

    /**
     * Set actuator state
     *
     * @param state (Type) new state of actuator
     * @return int (1 if state change was sucessful -1 otherwise)
     */
    virtual auto setState(StateType state) -> int = 0;

    /**
     * Get actuator state
     *
     * @return Type (1 if state change was sucessful -1 otherwise)
     */
    virtual auto getState() -> StateType = 0;

    const int id;
    const std::string name;

  protected:
    StateType state_ = false;
};
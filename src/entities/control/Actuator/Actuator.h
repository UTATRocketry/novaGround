template <typename StateType>
class Actuator {
  public:
    Actuator(int id) {this->id = id;};

    virtual auto setActuatorState(StateType state) -> int = 0;

    int id;

  protected:
    StateType actuator_state_ = false;
};
class BoolActuator {
  public:
    BoolActuator(int id) {this->id = id;};
    virtual auto setActuatorState(bool state) -> int = 0;

    int id;

  protected:
    bool actuator_state_ = false;
};
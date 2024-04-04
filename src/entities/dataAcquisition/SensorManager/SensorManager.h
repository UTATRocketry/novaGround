#pragma once

#include "../Sensor/Sensor.h"
#include <memory>
#include <vector>

class SensorManager {
  public:
    virtual auto initSensors() -> int = 0;
    virtual auto querySensor() -> double = 0;

    auto getNumSensors() -> int;

    // Get array of non owning pointers for sensors (keeps ownership in
    // SensorManager)
    auto getSensors() -> std::vector<Sensor*>;

  private:
    std::vector<std::unique_ptr<Sensor>> sensors_;
};
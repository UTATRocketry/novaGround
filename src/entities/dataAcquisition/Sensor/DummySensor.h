#pragma once

#include "Sensor.h"
#include <random>

class DummySensor : Sensor {
  public:
    DummySensor();
    auto readData() -> double override;

  private:
    std::uniform_real_distribution<double> unif_;
    std::default_random_engine r_eng_;
};

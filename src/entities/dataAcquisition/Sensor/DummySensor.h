#pragma once

#include "Sensor.h"
#include <random>

class DummySensor : private Sensor {
  public:
    explicit DummySensor(int id);
    auto readData() -> double override;

    int id;

  private:
    std::uniform_real_distribution<double> unif_;
    std::default_random_engine r_eng_;
};

#pragma once

#include "Sensor.h"
#include <random>
#include <string>

class DummySensor : public Sensor<double> {
  public:
    explicit DummySensor(int id, std::string name, double low, double high);
    auto query() -> double override;

  private:
    std::uniform_real_distribution<double> unif_;
    std::default_random_engine r_eng_;
};

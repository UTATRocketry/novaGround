#pragma once

#include "../Sensor/DummySensor.h"
#include "SensorManager.h"
#include <memory>
#include <string>
#include <tuple>
#include <vector>

class DummySensorManager : public SensorManager<double> {
  public:
    explicit DummySensorManager(
        std::vector<std::tuple<int, std::string, double, double>> params) {
        for (std::tuple<int, std::string, double, double>& sensor : params) {
            this->sensors_[std::get<0>(sensor)] = std::make_unique<DummySensor>(
                std::get<0>(sensor), std::get<1>(sensor), std::get<2>(sensor),
                std::get<3>(sensor));
        };
    };
};
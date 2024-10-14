#pragma once

#include <Actuator.h>
#include <Sensor.h>

#include <map>
#include <memory>
#include <tuple>
#include <vector>
#include <boost/thread/shared_mutex.hpp>


template <typename DataType> class SensorManager {
  public:
    SensorManager() {
        this->sensors_ = std::map<int, std::unique_ptr<Sensor<DataType>>>();
    };

    auto querySensor(int id) -> DataType {
        return this->sensors_.at(id)->query();
    };

    auto querySensors() -> std::vector<std::tuple<int, DataType>> {
        std::vector<std::tuple<int, DataType>> sensor_data{};
        for (auto& sensor : this->sensors_) {
            sensor_data.push_back({sensor.first, sensor.second->query()});
        }
        return sensor_data;
    };

    boost::shared_mutex mutex;

  protected:
    std::map<int, std::unique_ptr<Sensor<DataType>>> sensors_;
};
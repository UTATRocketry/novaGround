#include "SensorManager.h"

int SensorManager::getNumSensors() {
        return this->sensors_.size();
};

std::vector<Sensor *> SensorManager::getSensors() {
        std::vector<Sensor *> sensorReferences {};

        for (const std::unique_ptr<Sensor>& sensorPtr : this->sensors_) {
                sensorReferences.push_back(sensorPtr.get());
        }
        return sensorReferences;
};
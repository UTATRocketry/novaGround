#include "SensorManager.h"

int SensorManager::getNumSensors() {
        return this->sensors.size();
}

SensorManager::std::vector<Sensor *> getSensors() {
        std::vector<Sensor *> sensorReferences {};

        for (const std::unique_ptr<Sensor>& sensorPtr : this->sensors) {
                sensorReferences.push_back(sensorPtr.get());
        }
        return sensorReferences;
}
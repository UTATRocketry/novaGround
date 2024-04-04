#pragma once

#include <vector>
#include <string>
#include "../Sensor/Sensor.h"

class SensorTypeManager {
    public:

        virtual int initSensors() = 0;
        virtual double querySensor() = 0;

        int getNumSensors();

        // Get array of non owning pointers for sensors (keeps ownership in SensorManager)
        std::vector<Sensor *> getSensors();


    private:
        std::vector<std::unique_ptr<Sensor>> sensors;
};
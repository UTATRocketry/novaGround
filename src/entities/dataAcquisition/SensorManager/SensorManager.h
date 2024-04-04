#pragma once

#include <memory>
#include <vector>
#include "../Sensor/Sensor.h"

class SensorManager {
    public:

        virtual auto initSensors() -> int = 0;
        virtual auto querySensor() -> double = 0;

        auto getNumSensors() -> int;

        // Get array of non owning pointers for sensors (keeps ownership in SensorManager)
        auto getSensors() -> std::vector<Sensor *>;


    private:
        std::vector<std::unique_ptr<Sensor>> sensors_;
};
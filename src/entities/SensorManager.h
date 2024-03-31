#include <vector>
#include <string>
#include <Sensor.h>

class SensorTypeManager {
    public:
        virtual int initSensors();
        virtual double querySensor();

        int getNumSensors() {
            return sensors.size();
        }

        // Get array of non owning pointers for sensors (keeps ownership in SensorManager)
        std::vector<Sensor *> getSensors() {
            std::vector<Sensor *> sensorReferences {};

            for (const std::unique_ptr<Sensor>& sensorPtr : this->sensors) {
                sensorReferences.push_back(sensorPtr.get());
            }
            return sensorReferences;
        }


    private:
        std::vector<std::unique_ptr<Sensor>> sensors;
};
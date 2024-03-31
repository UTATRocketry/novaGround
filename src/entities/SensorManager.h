#include <vector>
#include <string>
#include <Sensor.h>

class SensorManager {
    public:
        virtual std::vector<Sensor *> initialize_sensors();
        virtual std::vector<Sensor *> get_available_sensors();

        std::vector<std::unique_ptr<Sensor>> sensors;
        std::string sensor_type;
};
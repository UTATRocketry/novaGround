#include <iostream>
#include <vector>
#include <random>
#include "Sensor.h"

class DummySensor : Sensor {
    public:
        DummySensor();
        int initialize(std::map<std::string, std::string> args) override;
        double read_data() override;
};

#include <iostream>
#include <vector>
#include <random>
#include "Sensor.h"

class DummySensor : Sensor {
    public:
        DummySensor(std::map<std::string, std::string> args);
        double read_data() override;

    private:
        std::uniform_real_distribution<double> unif;
        std:: default_random_engine re;
};

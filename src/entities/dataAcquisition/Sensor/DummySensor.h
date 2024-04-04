#include <iostream>
#include <vector>
#include <random>
#include "Sensor.h"

class dummy_Sensor : Sensor {
    public:
        dummy_Sensor();
        double readData() override;

    private:
        std::uniform_real_distribution<double> unif;
        std:: default_random_engine re;
};

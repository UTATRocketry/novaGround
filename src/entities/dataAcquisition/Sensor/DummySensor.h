#pragma once

#include <iostream>
#include <vector>
#include <random>
#include "Sensor.h"

class DummySensor : Sensor {
    public:
        DummySensor();
        double readData() override;

    private:
        std::uniform_real_distribution<double> unif;
        std:: default_random_engine re;
};

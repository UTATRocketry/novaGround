#pragma once
#include "DummySensor.h"


DummySensor::DummySensor() {
    const double lower = 10;
    const double upper = 100;
    const std::uniform_real_distribution<double> unif(lower, upper);
    const std:: default_random_engine re;

    this->re = re;
    this->unif = unif;
}

double DummySensor::readData() {
    return this->unif(this->re);
}

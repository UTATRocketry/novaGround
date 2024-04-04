#include "DummySensor.h"


dummy_Sensor::dummy_Sensor() {
    double lower = 10;
    double upper = 100;
    std::uniform_real_distribution<double> unif(lower, upper);
    std:: default_random_engine re;

    this->re = re;
    this->unif = unif;
}

double dummy_Sensor::readData() {
    return this->unif(this->re);
}

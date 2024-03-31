#include "DummySensor.h"


int DummySensor::initialize(std::map<std::string, std::string> args) {
    double lower = 10;
    double upper = 100;
    std::uniform_real_distribution<double> unif(lower, upper);
    std:: default_random_engine re;
    return unif(re);
}

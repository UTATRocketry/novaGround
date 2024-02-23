#include <iostream>
#include <vector>
#include <random>
#include "dummysensor.h"

double dummysensor(double lower, double upper) {
    std::uniform_real_distribution<double> unif(lower, upper);
    std:: default_random_engine re;
    return unif(re);
}

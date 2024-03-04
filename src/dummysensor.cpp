#include <iostream>
#include <vector>
#include <random>
#include "dummysensor.h"

double dummysensor(double lower, double upper) {
    std::random_device rd;
    std::default_random_engine re(rd());
    std::uniform_real_distribution<double> unif(lower, upper);
    return unif(re);
}

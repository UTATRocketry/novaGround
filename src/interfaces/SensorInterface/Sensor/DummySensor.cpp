#include "DummySensor.h"
#include "Sensor.h"
#include <string>

#include <random>
#include <utility>

DummySensor::DummySensor(int id, std::string name, double low, double high)
    : Sensor<double>(id, std::move(name)) {
    const std::uniform_real_distribution<double> unif(low, high);
    const std::default_random_engine r_eng;

    this->r_eng_ = r_eng;
    this->unif_ = unif;
}

auto DummySensor::query() -> double { return this->unif_(this->r_eng_); }
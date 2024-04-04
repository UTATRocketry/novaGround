#include "DummySensor.h"
#include "src/entities/dataAcquisition/Sensor/Sensor.h"
#include <random>

DummySensor::DummySensor(int id) : Sensor(id) {
    const std::uniform_real_distribution<double> unif(10, 100);
    const std::default_random_engine r_eng;

    this->r_eng_ = r_eng;
    this->unif_ = unif;
}

auto DummySensor::readData() -> double { return this->unif_(this->r_eng_); }

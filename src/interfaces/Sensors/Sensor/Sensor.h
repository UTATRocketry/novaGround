#pragma once

#include <map>
#include <string>

class Sensor {
    public:
        Sensor(int id) {this->id = id;};
        virtual double readData() = 0;

        int id;

};
#pragma once

#include <map>
#include <string>

class Sensor {
    public:
        virtual double readData() = 0;
};
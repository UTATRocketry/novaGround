#include <Sensor.h>

Sensor::Sensor() {

}

int Sensor::getValue(){
    return std::rand();
}
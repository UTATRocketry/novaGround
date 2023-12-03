#include <iostream>
#include <Sensor.h>
#include <unistd.h>

int main() {
    Sensor sensorOne = Sensor();
    while (true) {
        std::string command {};
        std::cin >> command;

        if (command == "start" || command == "Start"){
            int i {0};

            while (i++ < 10) {
                std::cout << sensorOne.getValue();
                sleep(500);
            }
        }
    }
}
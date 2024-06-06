#include "interfaces/ActuatorInterface/ActuatorManager/DummyActuatorManager.h"
#include "interfaces/SensorInterface/SensorManager/DummySensorManager.h"

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

auto main() -> int {
    std::tuple<int, std::string, bool> const actuator_one{0, "Actuator One",
                                                          true};
    std::tuple<int, std::string, bool> const actuator_two{1, "Actuator Two",
                                                          false};
    std::tuple<int, std::string, bool> const actuator_three{2, "Actuator Three",
                                                            true};

    std::vector<std::tuple<int, std::string, bool>> actuatorParams{
        actuator_one, actuator_two, actuator_three};

    auto dummyActuator = DummyActuatorManager(actuatorParams);

    std::vector<std::tuple<int, std::string, double, double>> sensorParams{
        {0, "Sensor One", 0, 10},
        {1, "Sensor Two", 10, 20},
        {2, "Sensor Three", 20, 30},
    };

    DummySensorManager dummySensorMan = DummySensorManager(sensorParams);
    std::cout << std::get<0>(dummySensorMan.querySensors()[0]);

    int i{0};
    while (i++ < 1000) {

        for (std::tuple<int, std::string, bool>& actuator_state :
             dummyActuator.getActuatorSummary()) {
            std::cout << "ID: " << std::get<0>(actuator_state) << '\n';
            std::cout << "Name: " << std::get<1>(actuator_state) << '\n';
            std::cout << "State: " << std::get<2>(actuator_state);
            std::cout << '\n' << '\n';
        }

        for (std::tuple<int, double>& sensorState :
             dummySensorMan.querySensors()) {
            std::cout << "ID:" << std::get<0>(sensorState) << "\n";
            std::cout << "Sensor Value" << std::get<1>(sensorState) << "\n";
            std::cout << "\n"
                      << "\n";
        }
    }
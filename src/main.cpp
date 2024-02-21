#include "actuators.cpp"
#include "sensors.cpp"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    std::cout << query_sensor(-1);
    bool initial_state = 1;
    std::cout << change_state(initial_state);
    return 0;
}

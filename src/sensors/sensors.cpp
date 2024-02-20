#include "sensors.h"

double query_sensor(int input_key) {
    //! should I do library including here as well?
    // if input_key = -1, then return dummy sensor
    srand((unsigned)time(NULL));
    if (input_key == -1) {
        double val = (rand() % 1000) / 1000.0;
        return val;
    }
    return -1.0; // failed to run
}

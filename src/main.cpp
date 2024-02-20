#include "sensors.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    time_t t;
    double read_out = query_sensor(-1);
    printf("Readout: %.3f\n", read_out);
    return 0;
}

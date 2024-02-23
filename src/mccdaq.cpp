#include <iostream>
#include <daqhats/mcc128.h>
#include <daqhats/daqhats.h>

double getDaqValue(int address, int channel) {
    double value;
    int result;
    uint32_t options = OPTS_DEFAULT;
    result = mcc128_a_in_read(address, channel, options , &value);
    return value;
}

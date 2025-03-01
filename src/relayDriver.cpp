#include <pigpio.h>
#include <iostream>

#define I2C_ADDR 0x00 // change this
#define RELAY_COUNT 16

int handle;
uint16_t relayState = 0xFFFF; // assuming that 1 corresponds to off and 0 corresponds to on

int initializeGpio(){
    if (gpioInitialise() < 0) {
        std::cout << "gpioInitialise failed";
        return -1;
    }
    handle = i2cOpen(1, I2C_ADDR, 0);
    if (handle < 0) {
        std::cout << "i2cOpen failed";
        return -1;
    }
    return 0;
}

int relayOn(int channel) {
    if (channel < 0 || channel >= RELAY_COUNT) {
        return -1;
    }
    relayState &= ~(1 << channel);
    i2cWriteWordData(handle, 0x09, relayState);
    return 0;
}

int relayOff(int channel) {
    if (channel < 0 || channel >= RELAY_COUNT) {
        return -1;
    }
    relayState |= (1 << channel);
    i2cWriteWordData(handle, 0x09, relayState);
    return 0;
}

int getRelayState() {
    return relayState;
}
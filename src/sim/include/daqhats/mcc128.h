/*
 * Simulated stand-in for the parts of MCC's <daqhats/mcc128.h> that novaGround
 * uses. See daqhats.h in this directory for what that means.
 */
#ifndef NOVA_SIM_MCC128_H
#define NOVA_SIM_MCC128_H

#include <stdint.h>

#include "daqhats.h"

#ifdef __cplusplus
extern "C" {
#endif

int mcc128_open(uint8_t address);
int mcc128_a_in_read(uint8_t address, uint8_t channel, uint32_t options, double* value);

#ifdef __cplusplus
}
#endif

#endif

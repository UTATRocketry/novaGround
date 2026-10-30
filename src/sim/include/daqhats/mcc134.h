/*
 * Simulated stand-in for the parts of MCC's <daqhats/mcc134.h> that novaGround
 * uses. See daqhats.h in this directory for what that means.
 */
#ifndef NOVA_SIM_MCC134_H
#define NOVA_SIM_MCC134_H

#include <stdint.h>

#include "daqhats.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Thermocouple types (simulator values). */
#define TC_TYPE_J 0
#define TC_TYPE_K 1
#define TC_TYPE_T 2
#define TC_TYPE_E 3
#define TC_TYPE_R 4
#define TC_TYPE_S 5
#define TC_TYPE_B 6
#define TC_TYPE_N 7

int mcc134_open(uint8_t address);
int mcc134_tc_type_write(uint8_t address, uint8_t channel, uint8_t type);
int mcc134_t_in_read(uint8_t address, uint8_t channel, double* value);

#ifdef __cplusplus
}
#endif

#endif

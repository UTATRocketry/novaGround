/*
 * Simulated stand-in for the parts of MCC's <daqhats/daqhats.h> that novaGround
 * uses. This is NOT MCC's header: the declarations are written to match the
 * documented C API (names, argument types, "negative result means error"), and
 * the numeric constants are this simulator's own. It is used only together with
 * libdaqhats_sim (src/sim/daqhats_sim.cpp); a real build includes MCC's header.
 */
#ifndef NOVA_SIM_DAQHATS_H
#define NOVA_SIM_DAQHATS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. Anything other than RESULT_SUCCESS is an error. */
#define RESULT_SUCCESS 0
#define RESULT_BAD_PARAMETER -1
#define RESULT_BUSY -2
#define RESULT_TIMEOUT -3
#define RESULT_LOCK_TIMEOUT -4
#define RESULT_INVALID_DEVICE -5
#define RESULT_RESOURCE_UNAVAIL -6
#define RESULT_COMMS_FAILURE -7
#define RESULT_UNDEFINED -10

/* HAT product ids (simulator values). */
#define HAT_ID_ANY 0
#define HAT_ID_MCC_134 0x0143
#define HAT_ID_MCC_128 0x0146

#define OPTS_DEFAULT 0

struct HatInfo {
    uint8_t address;
    uint16_t id;
    uint16_t version;
    char product_name[256];
};

/* Number of HATs found whose id matches filter_by_id (HAT_ID_ANY matches all).
 * If list is not NULL it receives that many entries. */
int hat_list(uint16_t filter_by_id, struct HatInfo* list);

#ifdef __cplusplus
}
#endif

#endif

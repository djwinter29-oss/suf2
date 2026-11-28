/*
 * Helpers for applications that run under the SUF2 RP2 loader.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog scratch[0] holds this value and scratch[1] its inverse. */
#define SUF2_UPDATE_REQUEST_MAGIC 0x53554632u /* "SUF2" */

/*
 * Reboot into the loader's update mode. The request lives in watchdog
 * scratch registers 0 and 1 and the loader clears it on the next boot, so
 * it is used once. Does not return.
 */
void suf2_app_request_update(void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

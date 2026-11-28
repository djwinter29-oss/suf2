/*
 * Flash regions owned by the loader: staging, the META journal sector and
 * the application slot. See firmware/README.md for the layout.
 */
#pragma once

#include <stdint.h>

#include "suf2/suf2.h"

typedef enum {
    META_EMPTY = 0,   /* no journal: nothing installed */
    META_PENDING,     /* journal written, install not committed */
    META_COMMITTED,   /* install finished and committed */
} meta_state_t;

/* Staging backend for the SUF2 session. */
extern const suf2_staging_t flash_staging;

/* Forget which staging sectors were erased; call with every session reset. */
void flash_store_session_reset(void);

suf2_reader_t flash_store_staging_reader(void);
suf2_reader_t flash_store_app_reader(void);

/* Reads the META sector. The manifest is copied out for PENDING and COMMITTED. */
meta_state_t flash_store_read_meta(uint8_t manifest[SUF2_MANIFEST_SIZE]);

/*
 * Re-verify the staged image, write the journal, copy staging into the
 * application slot, boot-verify the copy and commit. Safe to call again
 * after a power loss: the journal keeps the manifest.
 */
suf2_status_t flash_store_install(const uint8_t manifest[SUF2_MANIFEST_SIZE], const suf2_policy_t *policy);

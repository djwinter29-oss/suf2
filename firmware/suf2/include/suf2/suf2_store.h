/* Reusable staged installer for NOR flash with 4 KiB erase / 256-byte program units. */
#ifndef SUF2_STORE_H
#define SUF2_STORE_H

#include "suf2/suf2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUF2_STORE_SECTOR_SIZE 4096u
#define SUF2_STORE_PAGE_SIZE 256u

typedef enum {
    SUF2_META_EMPTY = 0,
    SUF2_META_PENDING,
    SUF2_META_COMMITTED,
} suf2_meta_state_t;

typedef struct {
    void *ctx;
    uint32_t flash_size;
    uint32_t meta_offset;
    uint32_t app_offset;
    uint32_t staging_offset;
    uint32_t slot_size;
    suf2_status_t (*read)(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len);
    suf2_status_t (*erase)(void *ctx, uint32_t offset, uint32_t len);
    /* Return success only after confirming that the bytes were programmed. */
    suf2_status_t (*program)(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len);
    uint8_t *erased_bitmap;
    uint32_t erased_bitmap_size;
} suf2_store_config_t;

/* Caller-owned state. Use one store per flash layout; do not call concurrently. */
typedef struct {
    suf2_store_config_t cfg;
    union {
        uint64_t align;
        uint8_t bytes[SUF2_STORE_SECTOR_SIZE];
    } scratch;
} suf2_store_t;

/* Validates disjoint regions and callback/buffer requirements.
 * Must succeed before any other store API is called.
 */
suf2_status_t suf2_store_init(suf2_store_t *store, const suf2_store_config_t *cfg);
/* Forget erased staging sectors at the start of each receiver session. */
void suf2_store_session_reset(suf2_store_t *store);
suf2_staging_t suf2_store_staging(suf2_store_t *store);
suf2_reader_t suf2_store_staging_reader(suf2_store_t *store);
suf2_reader_t suf2_store_app_reader(suf2_store_t *store);

/* Copy the retained manifest for PENDING and COMMITTED; surface read errors. */
suf2_status_t suf2_store_read_meta(suf2_store_t *store, uint8_t manifest[SUF2_MANIFEST_SIZE],
                                  suf2_meta_state_t *state);
/* Reverify, journal, copy, boot-verify, then commit. May resume after power loss. */
suf2_status_t suf2_store_install(suf2_store_t *store, const uint8_t manifest[SUF2_MANIFEST_SIZE],
                                const suf2_policy_t *policy);

#ifdef __cplusplus
}
#endif
#endif

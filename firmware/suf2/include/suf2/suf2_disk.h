/* Virtual FAT16 update drive, independent of the USB stack. */
#ifndef SUF2_DISK_H
#define SUF2_DISK_H

#include "suf2/suf2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SUF2_DISK_SECTOR_COUNT 64069u
#define SUF2_DISK_TEXT_SIZE 4096u

typedef struct {
    void *ctx;
    /* Return bytes written, excluding the terminator, less than buffer size. */
    size_t (*info_text)(void *ctx, char *buf, size_t size);
    size_t (*status_text)(void *ctx, char *buf, size_t size);
    /* A complete host-written sector; the receiver reports rejection separately. */
    void (*on_sector)(void *ctx, const uint8_t sector[SUF2_BLOCK_SIZE]);
} suf2_disk_config_t;

/* Caller-owned, single-transport state; calls must be serialized. */
typedef struct {
    suf2_disk_config_t cfg;
    char text[SUF2_DISK_TEXT_SIZE];
    uint8_t write_buf[SUF2_BLOCK_SIZE];
    uint8_t sector[SUF2_BLOCK_SIZE];
} suf2_disk_t;

/* Must succeed before any read or write call. */
suf2_status_t suf2_disk_init(suf2_disk_t *disk, const suf2_disk_config_t *cfg);
suf2_status_t suf2_disk_read(suf2_disk_t *disk, uint32_t lba, uint32_t offset,
                             uint8_t *buffer, uint32_t size);
/* Writes ignore LBA: SUF2 sectors carry their own identity.
 * Partial sectors must arrive in order; the transport must not interleave them.
 */
suf2_status_t suf2_disk_write(suf2_disk_t *disk, uint32_t offset, const uint8_t *buffer, uint32_t size);

#ifdef __cplusplus
}
#endif
#endif

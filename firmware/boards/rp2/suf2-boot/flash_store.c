#include "flash_store.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"
#include "suf2_board_config.h"

_Static_assert(FLASH_SECTOR_SIZE == SUF2_STORE_SECTOR_SIZE, "unsupported flash erase unit");
_Static_assert(FLASH_PAGE_SIZE == SUF2_STORE_PAGE_SIZE, "unsupported flash program unit");

static suf2_store_t store;
static uint8_t erased[(SUF2_SLOT_SIZE / FLASH_SECTOR_SIZE + 7) / 8];
suf2_staging_t flash_staging;

static const uint8_t *xip(uint32_t offset)
{
    return (const uint8_t *)(XIP_BASE + offset);
}

static suf2_status_t read_flash(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (len > SUF2_FLASH_SIZE || offset > SUF2_FLASH_SIZE - len) return SUF2_E_STORAGE;
    memcpy(buf, xip(offset), len);
    return SUF2_OK;
}

static bool writable(uint32_t offset, uint32_t len)
{
    return offset >= SUF2_META_OFFSET && len <= SUF2_FLASH_SIZE && offset <= SUF2_FLASH_SIZE - len;
}

static suf2_status_t erase_flash(void *ctx, uint32_t offset, uint32_t len)
{
    (void)ctx;
    if (!writable(offset, len) || offset % FLASH_SECTOR_SIZE || len % FLASH_SECTOR_SIZE)
        return SUF2_E_STORAGE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, len);
    restore_interrupts(irq);
    return SUF2_OK;
}

static suf2_status_t program_flash(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    if (!writable(offset, len) || offset % FLASH_PAGE_SIZE || len % FLASH_PAGE_SIZE)
        return SUF2_E_STORAGE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(offset, data, len);
    restore_interrupts(irq);
    return memcmp(xip(offset), data, len) == 0 ? SUF2_OK : SUF2_E_STORAGE;
}

suf2_status_t flash_store_init(void)
{
    const suf2_store_config_t cfg = {
        .flash_size = SUF2_FLASH_SIZE,
        .meta_offset = SUF2_META_OFFSET,
        .app_offset = SUF2_APP_OFFSET,
        .staging_offset = SUF2_STAGING_OFFSET,
        .slot_size = SUF2_SLOT_SIZE,
        .read = read_flash, .erase = erase_flash, .program = program_flash,
        .erased_bitmap = erased, .erased_bitmap_size = sizeof erased,
    };
    suf2_status_t st = suf2_store_init(&store, &cfg);
    if (st == SUF2_OK) flash_staging = suf2_store_staging(&store);
    return st;
}

void flash_store_session_reset(void)
{
    suf2_store_session_reset(&store);
}

suf2_reader_t flash_store_staging_reader(void)
{
    return suf2_store_staging_reader(&store);
}

suf2_reader_t flash_store_app_reader(void)
{
    return suf2_store_app_reader(&store);
}

suf2_status_t flash_store_read_meta(uint8_t manifest[SUF2_MANIFEST_SIZE], meta_state_t *state)
{
    return suf2_store_read_meta(&store, manifest, state);
}

suf2_status_t flash_store_install(const uint8_t manifest[SUF2_MANIFEST_SIZE], const suf2_policy_t *policy)
{
    return suf2_store_install(&store, manifest, policy);
}

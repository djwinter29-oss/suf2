#include <string.h>

#include "flash_port.h"
#include "suf2_config.h"

#ifdef BOOTLOADER_BUILD
#include "bootloader_flash_priv.h"
#else
#include "esp_flash.h"
#endif

static bool within(uint32_t offset, uint32_t len, uint32_t start, uint32_t size)
{
    return offset >= start && len <= size && offset - start <= size - len;
}

static bool writable(uint32_t offset, uint32_t len)
{
    return within(offset, len, SUF2_META_OFFSET, SUF2_STORE_SECTOR_SIZE) ||
           within(offset, len, SUF2_APP_OFFSET, SUF2_SLOT_SIZE) ||
           within(offset, len, SUF2_STAGING_OFFSET, SUF2_SLOT_SIZE);
}

static suf2_status_t read_flash(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (!within(offset, len, 0, SUF2_FLASH_SIZE)) return SUF2_E_STORAGE;
#ifdef BOOTLOADER_BUILD
    /* ROM reads require aligned addresses, buffers and lengths. */
    uint32_t words[64];
    while (len) {
        uint32_t in = offset % 4u;
        uint32_t n = len < sizeof words - in ? len : sizeof words - in;
        uint32_t aligned = (in + n + 3u) & ~3u;
        if (bootloader_flash_read(offset - in, words, aligned, false) != ESP_OK) return SUF2_E_STORAGE;
        memcpy(buf, (uint8_t *)words + in, n);
        buf += n; offset += n; len -= n;
    }
    return SUF2_OK;
#else
    return esp_flash_read(NULL, buf, offset, len) == ESP_OK ? SUF2_OK : SUF2_E_STORAGE;
#endif
}

static suf2_status_t erase_flash(void *ctx, uint32_t offset, uint32_t len)
{
    (void)ctx;
    if (!writable(offset, len) || offset % SUF2_STORE_SECTOR_SIZE || len % SUF2_STORE_SECTOR_SIZE)
        return SUF2_E_STORAGE;
#ifdef BOOTLOADER_BUILD
    return bootloader_flash_erase_range(offset, len) == ESP_OK ? SUF2_OK : SUF2_E_STORAGE;
#else
    return esp_flash_erase_region(NULL, offset, len) == ESP_OK ? SUF2_OK : SUF2_E_STORAGE;
#endif
}

static suf2_status_t program_flash(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (!writable(offset, len) || offset % SUF2_STORE_PAGE_SIZE || len % SUF2_STORE_PAGE_SIZE)
        return SUF2_E_STORAGE;
    uint32_t words[SUF2_STORE_PAGE_SIZE / 4];
    uint8_t check[SUF2_STORE_PAGE_SIZE];
    for (uint32_t off = 0; off < len; off += SUF2_STORE_PAGE_SIZE) {
        memcpy(words, data + off, sizeof words);
#ifdef BOOTLOADER_BUILD
        esp_err_t result = bootloader_flash_write(offset + off, words, sizeof words, false);
#else
        esp_err_t result = esp_flash_write(NULL, words, offset + off, sizeof words);
#endif
        if (result != ESP_OK || read_flash(ctx, offset + off, check, sizeof check) != SUF2_OK ||
            memcmp(check, words, sizeof check) != 0) return SUF2_E_STORAGE;
    }
    return SUF2_OK;
}

suf2_status_t suf2_esp32_store_init(suf2_store_t *store, uint8_t *bitmap, uint32_t bitmap_size)
{
    const suf2_store_config_t cfg = {
        .flash_size = SUF2_FLASH_SIZE, .meta_offset = SUF2_META_OFFSET,
        .app_offset = SUF2_APP_OFFSET, .staging_offset = SUF2_STAGING_OFFSET,
        .slot_size = SUF2_SLOT_SIZE,
        .read = read_flash, .erase = erase_flash, .program = program_flash,
        .erased_bitmap = bitmap, .erased_bitmap_size = bitmap_size,
    };
    return suf2_store_init(store, &cfg);
}

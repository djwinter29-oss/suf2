#include "flash_store.h"

#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/platform.h"

#include "suf2_board_config.h"

#define SECTOR FLASH_SECTOR_SIZE
#define PAGE FLASH_PAGE_SIZE

#define JOURNAL_MAGIC 0x4C4A3253u /* "S2JL" */
#define COMMIT_MAGIC 0x4B4F3253u  /* "S2OK" */
#define JOURNAL_VERSION 1u
#define COMMIT_OFFSET (SECTOR - PAGE)

_Static_assert(SUF2_META_OFFSET % SECTOR == 0, "META must be sector aligned");
_Static_assert(SUF2_APP_OFFSET % SECTOR == 0, "slot must be sector aligned");
_Static_assert(SUF2_STAGING_OFFSET % SECTOR == 0, "staging must be sector aligned");
_Static_assert(SUF2_SLOT_SIZE % SECTOR == 0, "slot size must be whole sectors");
_Static_assert(SUF2_APP_OFFSET + SUF2_SLOT_SIZE <= SUF2_STAGING_OFFSET, "slot overlaps staging");
_Static_assert(SUF2_STAGING_OFFSET + SUF2_SLOT_SIZE <= SUF2_FLASH_SIZE, "staging past end of flash");

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    uint32_t check; /* ~magic */
} journal_t;

typedef struct {
    uint32_t magic;
    uint8_t package_id[SUF2_ID_SIZE];
    uint32_t check; /* ~magic */
} commit_t;

_Static_assert(sizeof(journal_t) <= 2 * PAGE, "journal must fit two pages");

typedef struct {
    uint32_t offset;
    uint32_t size;
} region_t;

static const region_t staging_region = {SUF2_STAGING_OFFSET, SUF2_SLOT_SIZE};
static const region_t app_region = {SUF2_APP_OFFSET, SUF2_SLOT_SIZE};

static uint8_t erased[(SUF2_SLOT_SIZE / SECTOR + 7) / 8];
static uint8_t copy_buf[SECTOR] __attribute__((aligned(4)));

static const uint8_t *xip(uint32_t offset)
{
    return (const uint8_t *)(XIP_BASE + offset);
}

/* Nothing below META may ever be erased or programmed: that is the loader. */
static bool writable(uint32_t offset, uint32_t len)
{
    return offset >= SUF2_META_OFFSET && len <= SUF2_FLASH_SIZE && offset <= SUF2_FLASH_SIZE - len;
}

static suf2_status_t erase(uint32_t offset, uint32_t len)
{
    if (!writable(offset, len) || offset % SECTOR || len % SECTOR) return SUF2_E_STORAGE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(offset, len);
    restore_interrupts(irq);
    return SUF2_OK;
}

static suf2_status_t program(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (!writable(offset, len) || offset % PAGE || len % PAGE) return SUF2_E_STORAGE;
    uint32_t irq = save_and_disable_interrupts();
    flash_range_program(offset, data, len);
    restore_interrupts(irq);
    return memcmp(xip(offset), data, len) == 0 ? SUF2_OK : SUF2_E_STORAGE;
}

static suf2_status_t region_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const region_t *r = ctx;
    if (len > r->size || offset > r->size - len) return SUF2_E_STORAGE;
    memcpy(buf, xip(r->offset + offset), len);
    return SUF2_OK;
}

static suf2_status_t staging_write_chunk(void *ctx, uint32_t index, const uint8_t data[SUF2_CHUNK_SIZE])
{
    (void)ctx;
    if (index >= SUF2_SLOT_SIZE / SUF2_CHUNK_SIZE) return SUF2_E_STORAGE;
    uint32_t rel = index * SUF2_CHUNK_SIZE;
    uint32_t sector = rel / SECTOR;
    if (!(erased[sector / 8] & (1u << (sector % 8)))) {
        suf2_status_t st = erase(SUF2_STAGING_OFFSET + sector * SECTOR, SECTOR);
        if (st != SUF2_OK) return st;
        erased[sector / 8] |= (uint8_t)(1u << (sector % 8));
    }
    return program(SUF2_STAGING_OFFSET + rel, data, SUF2_CHUNK_SIZE);
}

_Static_assert(SUF2_CHUNK_SIZE == PAGE, "one chunk is programmed as one page");

const suf2_staging_t flash_staging = {
    .ctx = (void *)&staging_region,
    .write_chunk = staging_write_chunk,
    .read = region_read,
};

void flash_store_session_reset(void)
{
    memset(erased, 0, sizeof erased);
}

suf2_reader_t flash_store_staging_reader(void)
{
    return (suf2_reader_t){.ctx = (void *)&staging_region, .read = region_read};
}

suf2_reader_t flash_store_app_reader(void)
{
    return (suf2_reader_t){.ctx = (void *)&app_region, .read = region_read};
}

meta_state_t flash_store_read_meta(uint8_t manifest[SUF2_MANIFEST_SIZE])
{
    journal_t j;
    commit_t c;
    uint8_t id[SUF2_ID_SIZE];
    memcpy(&j, xip(SUF2_META_OFFSET), sizeof j);
    if (j.magic != JOURNAL_MAGIC || j.check != ~JOURNAL_MAGIC || j.version != JOURNAL_VERSION) return META_EMPTY;
    memcpy(manifest, j.manifest, SUF2_MANIFEST_SIZE);
    memcpy(&c, xip(SUF2_META_OFFSET + COMMIT_OFFSET), sizeof c);
    if (c.magic != COMMIT_MAGIC || c.check != ~COMMIT_MAGIC) return META_PENDING;
    if (suf2_package_id(manifest, id) != SUF2_OK || memcmp(id, c.package_id, SUF2_ID_SIZE) != 0) return META_PENDING;
    return META_COMMITTED;
}

suf2_status_t flash_store_install(const uint8_t manifest[SUF2_MANIFEST_SIZE], const suf2_policy_t *policy)
{
    suf2_manifest_t info;
    suf2_reader_t staged = flash_store_staging_reader();
    suf2_reader_t installed = flash_store_app_reader();
    uint8_t id[SUF2_ID_SIZE];

    /* Last check before anything destructive happens. */
    suf2_status_t st = suf2_verify_staged(manifest, policy, &staged);
    if (st != SUF2_OK) return st;
    if ((st = suf2_manifest_parse(manifest, &info)) != SUF2_OK) return st;
    if ((st = suf2_package_id(manifest, id)) != SUF2_OK) return st;
    if (info.image_length > SUF2_SLOT_SIZE) return SUF2_E_POLICY_SIZE;

    /* Journal first: from here on the old application is no longer bootable. */
    static journal_t j;
    memset(copy_buf, 0xff, 2 * PAGE);
    j = (journal_t){.magic = JOURNAL_MAGIC, .version = JOURNAL_VERSION, .check = ~JOURNAL_MAGIC};
    memcpy(j.manifest, manifest, SUF2_MANIFEST_SIZE);
    memcpy(copy_buf, &j, sizeof j);
    if ((st = erase(SUF2_META_OFFSET, SECTOR)) != SUF2_OK) return st;
    if ((st = program(SUF2_META_OFFSET, copy_buf, 2 * PAGE)) != SUF2_OK) return st;

    uint32_t span = (info.image_length + SECTOR - 1) / SECTOR * SECTOR;
    for (uint32_t off = 0; off < span; off += SECTOR) {
        memcpy(copy_buf, xip(SUF2_STAGING_OFFSET + off), SECTOR);
        if ((st = erase(SUF2_APP_OFFSET + off, SECTOR)) != SUF2_OK) return st;
        if ((st = program(SUF2_APP_OFFSET + off, copy_buf, SECTOR)) != SUF2_OK) return st;
    }

    if ((st = suf2_verify_boot(manifest, policy, &installed)) != SUF2_OK) return st;

    commit_t c = {.magic = COMMIT_MAGIC, .check = ~COMMIT_MAGIC};
    memcpy(c.package_id, id, SUF2_ID_SIZE);
    memset(copy_buf, 0xff, PAGE);
    memcpy(copy_buf, &c, sizeof c);
    return program(SUF2_META_OFFSET + COMMIT_OFFSET, copy_buf, PAGE);
}

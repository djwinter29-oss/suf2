#include <string.h>

#include "unity.h"
#include "suf2/suf2_store.h"
#include "mock_suf2_crypto.h"

TEST_SOURCE_FILE("suf2.c")

static uint8_t flash[3 * SUF2_STORE_SECTOR_SIZE];
static uint8_t erased[1];
static suf2_store_t store;
static suf2_store_config_t cfg;
static bool read_failure;
static unsigned erases;

static suf2_status_t read_flash(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    if (read_failure || len > sizeof flash || offset > sizeof flash - len) return SUF2_E_STORAGE;
    memcpy(buf, flash + offset, len);
    return SUF2_OK;
}

static suf2_status_t erase_flash(void *ctx, uint32_t offset, uint32_t len)
{
    (void)ctx;
    if (len > sizeof flash || offset > sizeof flash - len) return SUF2_E_STORAGE;
    memset(flash + offset, 0xff, len);
    erases++;
    return SUF2_OK;
}

static suf2_status_t program_flash(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    (void)ctx;
    if (len > sizeof flash || offset > sizeof flash - len) return SUF2_E_STORAGE;
    memcpy(flash + offset, data, len);
    return SUF2_OK;
}

void setUp(void)
{
    memset(flash, 0xff, sizeof flash);
    read_failure = false;
    erases = 0;
    cfg = (suf2_store_config_t){
        .flash_size = sizeof flash, .meta_offset = 0,
        .app_offset = 4096, .staging_offset = 8192, .slot_size = 4096,
        .read = read_flash, .erase = erase_flash, .program = program_flash,
        .erased_bitmap = erased, .erased_bitmap_size = sizeof erased,
    };
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_store_init(&store, &cfg));
}

void tearDown(void) {}

void test_store_rejects_overlapping_regions_and_undersized_bitmap(void)
{
    suf2_store_config_t invalid = cfg;
    invalid.staging_offset = invalid.app_offset;
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_store_init(&store, &invalid));
    invalid = cfg;
    invalid.erased_bitmap_size = 0;
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_store_init(&store, &invalid));
    invalid = cfg;
    invalid.meta_offset = UINT32_MAX - 4095u;
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_store_init(&store, &invalid));
}

void test_staging_erases_once_per_sector_and_reads_back_written_chunks(void)
{
    uint8_t chunk[SUF2_CHUNK_SIZE], readback[SUF2_CHUNK_SIZE];
    memset(chunk, 0x5a, sizeof chunk);
    suf2_staging_t staging = suf2_store_staging(&store);
    TEST_ASSERT_EQUAL(SUF2_OK, staging.write_chunk(staging.ctx, 0, chunk));
    TEST_ASSERT_EQUAL(SUF2_OK, staging.write_chunk(staging.ctx, 1, chunk));
    TEST_ASSERT_EQUAL_UINT32(1, erases);
    TEST_ASSERT_EQUAL(SUF2_OK, staging.read(staging.ctx, SUF2_CHUNK_SIZE, readback, sizeof readback));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(chunk, readback, sizeof chunk);
    suf2_store_session_reset(&store);
    TEST_ASSERT_EQUAL(SUF2_OK, staging.write_chunk(staging.ctx, 0, chunk));
    TEST_ASSERT_EQUAL_UINT32(2, erases);
}

void test_staging_rejects_access_outside_its_slot(void)
{
    uint8_t chunk[SUF2_CHUNK_SIZE];
    suf2_staging_t staging = suf2_store_staging(&store);
    TEST_ASSERT_EQUAL(SUF2_E_STORAGE, staging.write_chunk(staging.ctx, cfg.slot_size / SUF2_CHUNK_SIZE, chunk));
    TEST_ASSERT_EQUAL(SUF2_E_STORAGE, staging.read(staging.ctx, cfg.slot_size, chunk, 1));
}

void test_metadata_read_failure_is_reported_to_the_caller(void)
{
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    suf2_meta_state_t state;
    read_failure = true;
    TEST_ASSERT_EQUAL(SUF2_E_STORAGE, suf2_store_read_meta(&store, manifest, &state));
}

#include <string.h>

#include "unity.h"
#include "suf2/suf2_store.h"
#include "mock_suf2_crypto.h"

TEST_SOURCE_FILE("suf2.c")
TEST_SOURCE_FILE("suf2_store.c")

static uint8_t flash[3 * SUF2_STORE_SECTOR_SIZE];
static uint8_t erased[1];
static suf2_store_t store;
static suf2_store_config_t cfg;
static bool read_failure;
static unsigned erases;
static const uint8_t zero_digest[SUF2_DIGEST_SIZE];
static uint8_t plaintext[17];
static uint32_t block_output = 16, tail_output = 1;

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

static void expect_hash(unsigned updates)
{
    suf2_sha256_start_ExpectAnyArgsAndReturn(SUF2_OK);
    for (unsigned i = 0; i < updates; i++) suf2_sha256_update_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ReturnArrayThruPtr_digest(zero_digest, sizeof zero_digest);
}

static void expect_signature(void)
{
    expect_hash(1);
    expect_hash(2);
    suf2_p256_verify_ExpectAnyArgsAndReturn(SUF2_OK);
}

static void expect_decrypt_update(void)
{
    suf2_gcm_update_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_gcm_update_ReturnArrayThruPtr_output(plaintext, block_output);
    suf2_gcm_update_ReturnThruPtr_written(&block_output);
}

static void expect_decrypt_finish(suf2_status_t status)
{
    suf2_gcm_finish_ExpectAnyArgsAndReturn(status);
    suf2_gcm_finish_ReturnArrayThruPtr_output(plaintext + block_output, tail_output);
    suf2_gcm_finish_ReturnThruPtr_written(&tail_output);
}

static void expect_encrypted_verification(suf2_status_t tag_status)
{
    expect_signature();
    expect_hash(1);
    suf2_gcm_start_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_start_ExpectAnyArgsAndReturn(SUF2_OK);
    expect_decrypt_update();
    suf2_sha256_update_ExpectAnyArgsAndReturn(SUF2_OK);
    expect_decrypt_finish(tag_status);
    if (tag_status == SUF2_OK) suf2_sha256_update_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ReturnArrayThruPtr_digest(zero_digest, sizeof zero_digest);
}

static void encrypted_manifest(uint8_t manifest[SUF2_MANIFEST_SIZE])
{
    memset(manifest, 0, SUF2_MANIFEST_SIZE);
    memcpy(manifest, "SUF2MFST", 8);
    manifest[SUF2_M_VERSION] = 1;
    manifest[SUF2_M_FLAGS] = SUF2_MANIFEST_FLAG_ENCRYPTED;
    manifest[SUF2_M_IMAGE_LENGTH] = sizeof plaintext;
    manifest[SUF2_M_ENCRYPTION_KEY_ID] = 1;
    manifest[SUF2_M_SIGNATURE + 31] = 1;
    manifest[SUF2_M_SIGNATURE + 63] = 1;
    memset(flash + cfg.staging_offset, 0xaa, sizeof plaintext);
    memset(plaintext, 0x33, sizeof plaintext);
    plaintext[16] = 0x44;
}

void test_encrypted_install_includes_plaintext_buffered_until_gcm_finish(void)
{
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    const suf2_signing_key_t signing = {0};
    const suf2_decryption_key_t aes = {.key_id = {1}};
    const suf2_policy_t policy = {.max_image_size = cfg.slot_size, .keys = &signing, .key_count = 1,
        .decryption_keys = &aes, .decryption_key_count = 1};
    encrypted_manifest(manifest);
    expect_encrypted_verification(SUF2_OK);
    expect_hash(1);
    suf2_gcm_start_ExpectAnyArgsAndReturn(SUF2_OK);
    expect_decrypt_update();
    expect_decrypt_finish(SUF2_OK);
    expect_signature();
    expect_hash(1);
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_store_install(&store, manifest, &policy));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plaintext, flash + cfg.app_offset, sizeof plaintext);
    TEST_ASSERT_EQUAL_HEX8(0xaa, flash[cfg.staging_offset]);
    TEST_ASSERT_EQUAL_UINT32(2, erases);
    suf2_meta_state_t meta;
    expect_hash(1);
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_store_read_meta(&store, manifest, &meta));
    TEST_ASSERT_EQUAL(SUF2_META_COMMITTED, meta);
}

void test_bad_gcm_tag_releases_context_without_erasing_metadata_or_application(void)
{
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    const suf2_signing_key_t signing = {0};
    const suf2_decryption_key_t aes = {.key_id = {1}};
    const suf2_policy_t policy = {.max_image_size = cfg.slot_size, .keys = &signing, .key_count = 1,
        .decryption_keys = &aes, .decryption_key_count = 1};
    encrypted_manifest(manifest);
    expect_encrypted_verification(SUF2_E_GCM_TAG);
    TEST_ASSERT_EQUAL(SUF2_E_GCM_TAG, suf2_store_install(&store, manifest, &policy));
    TEST_ASSERT_EQUAL_UINT32(0, erases);
    TEST_ASSERT_EQUAL_HEX8(0xff, flash[cfg.meta_offset]);
    TEST_ASSERT_EQUAL_HEX8(0xff, flash[cfg.app_offset]);
}

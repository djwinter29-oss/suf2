#include <string.h>

#include "unity.h"
#include "suf2/suf2.h"
#include "mock_suf2_crypto.h"

static uint8_t manifest[SUF2_MANIFEST_SIZE];

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(value >> (8 * i));
}

void setUp(void)
{
    memset(manifest, 0, sizeof manifest);
    memcpy(manifest, "SUF2MFST", 8);
    put32(manifest + SUF2_M_VERSION, SUF2_FORMAT_VERSION);
    put32(manifest + SUF2_M_LOAD_ADDRESS, 0x10011000);
    put32(manifest + SUF2_M_IMAGE_LENGTH, 257);
}

void tearDown(void) {}

void test_manifest_retains_address_and_length(void)
{
    suf2_manifest_t parsed;
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_manifest_parse(manifest, &parsed));
    TEST_ASSERT_EQUAL_HEX32(0x10011000, parsed.load_address);
    TEST_ASSERT_EQUAL_UINT32(257, parsed.image_length);
}

void test_manifest_rejects_reserved_bytes_and_unsigned_encryption_fields(void)
{
    manifest[SUF2_M_RESERVED1] = 1;
    TEST_ASSERT_EQUAL(SUF2_E_MANIFEST_FORMAT, suf2_manifest_parse(manifest, NULL));
    manifest[SUF2_M_RESERVED1] = 0;
    manifest[SUF2_M_NONCE] = 1;
    TEST_ASSERT_EQUAL(SUF2_E_MANIFEST_FORMAT, suf2_manifest_parse(manifest, NULL));
}

void test_package_id_returns_first_sixteen_digest_bytes(void)
{
    uint8_t digest[SUF2_DIGEST_SIZE], id[SUF2_ID_SIZE];
    for (unsigned i = 0; i < sizeof digest; i++) digest[i] = (uint8_t)(i + 1);
    suf2_sha256_start_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_update_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ReturnArrayThruPtr_digest(digest, sizeof digest);
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_package_id(manifest, id));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(digest, id, sizeof id);
}

void test_failed_hash_update_still_releases_context_and_preserves_output(void)
{
    uint8_t id[SUF2_ID_SIZE], unchanged[SUF2_ID_SIZE];
    memset(id, 0xa5, sizeof id);
    memcpy(unchanged, id, sizeof id);
    suf2_sha256_start_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_update_ExpectAnyArgsAndReturn(SUF2_E_CRYPTO_BACKEND);
    suf2_sha256_finish_ExpectAnyArgsAndReturn(SUF2_OK);
    TEST_ASSERT_EQUAL(SUF2_E_CRYPTO_BACKEND, suf2_package_id(manifest, id));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(unchanged, id, sizeof id);
}

static suf2_status_t failed_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx; (void)offset; (void)buf; (void)len;
    return SUF2_E_STORAGE;
}

void test_hash_reader_failure_is_not_reported_as_a_valid_digest(void)
{
    suf2_reader_t reader = {.read = failed_read};
    uint8_t digest[SUF2_DIGEST_SIZE];
    suf2_sha256_start_ExpectAnyArgsAndReturn(SUF2_OK);
    suf2_sha256_finish_ExpectAnyArgsAndReturn(SUF2_OK);
    TEST_ASSERT_EQUAL(SUF2_E_STORAGE, suf2_hash_image(&reader, 1, digest));
}

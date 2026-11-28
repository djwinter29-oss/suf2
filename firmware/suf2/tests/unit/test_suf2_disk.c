#include <string.h>

#include "unity.h"
#include "suf2/suf2_disk.h"

TEST_SOURCE_FILE("suf2_disk.c")

static suf2_disk_t disk;
static uint8_t received[SUF2_BLOCK_SIZE];
static unsigned received_count;
static bool render_failure;

static size_t text(void *ctx, char *buf, size_t size)
{
    (void)ctx;
    if (render_failure) return size;
    memcpy(buf, "Ready\r\n", 7);
    return 7;
}

static void receive(void *ctx, const uint8_t sector[SUF2_BLOCK_SIZE])
{
    (void)ctx;
    memcpy(received, sector, sizeof received);
    received_count++;
}

void setUp(void)
{
    received_count = 0;
    render_failure = false;
    const suf2_disk_config_t cfg = {.info_text = text, .status_text = text, .on_sector = receive};
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_disk_init(&disk, &cfg));
}

void tearDown(void) {}

void test_partial_writes_deliver_one_complete_unchanged_sector(void)
{
    uint8_t data[SUF2_BLOCK_SIZE];
    for (unsigned i = 0; i < sizeof data; i++) data[i] = (uint8_t)i;
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_disk_write(&disk, 0, data, 127));
    TEST_ASSERT_EQUAL_UINT32(0, received_count);
    TEST_ASSERT_EQUAL(SUF2_OK, suf2_disk_write(&disk, 127, data + 127, sizeof data - 127));
    TEST_ASSERT_EQUAL_UINT32(1, received_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, received, sizeof data);
}

void test_disk_rejects_out_of_range_and_overflowing_transfers(void)
{
    uint8_t data[SUF2_BLOCK_SIZE] = {0};
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_disk_read(&disk, SUF2_DISK_SECTOR_COUNT, 0, data, sizeof data));
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_disk_read(&disk, SUF2_DISK_SECTOR_COUNT - 1, 1, data, sizeof data));
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_disk_write(&disk, UINT32_MAX, data, 1));
}

void test_failed_text_renderer_is_not_reported_as_a_successful_read(void)
{
    uint8_t data[SUF2_BLOCK_SIZE];
    render_failure = true;
    TEST_ASSERT_EQUAL(SUF2_E_ARGUMENT, suf2_disk_read(&disk, 65, 0, data, sizeof data));
}

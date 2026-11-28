#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "flash_port.h"
#include "suf2_config.h"
#include "suf2_keys.h"
#include "suf2_aes_keys.h"
#include "usb.h"

static const char *TAG = "suf2";
static suf2_store_t store;
static suf2_session_t session;
static suf2_disk_t disk;
static suf2_staging_t staging;
static uint8_t erased[(SUF2_SLOT_SIZE / SUF2_STORE_SECTOR_SIZE + 7u) / 8u];
static uint8_t blocks[(SUF2_SLOT_SIZE / SUF2_CHUNK_SIZE + 8u) / 8u];
static SemaphoreHandle_t lock;
static bool installing;
static suf2_status_t last_error;
static const suf2_policy_t policy = {
    .product_id = SUF2_PRODUCT_ID_BYTES, .execution_base = SUF2_EXECUTION_BASE,
    .max_image_size = SUF2_SLOT_SIZE, .keys = suf2_trusted_keys, .key_count = SUF2_TRUSTED_KEY_COUNT,
    .decryption_keys = suf2_decryption_keys, .decryption_key_count = SUF2_DECRYPTION_KEY_COUNT,
};

static void check(suf2_status_t status)
{
    if (status != SUF2_OK) {
        ESP_LOGE(TAG, "startup: %s", suf2_status_name(status));
        abort();
    }
}

static size_t info_text(void *ctx, char *buf, size_t size)
{
    (void)ctx;
    int n = snprintf(buf, size, "SUF2 ESP32-S3 development loader\r\n"
        "Product ID: %s\r\nExecution window: 0x%08lx\r\nLargest image: %lu bytes\r\n"
        "Authenticated ESP-IDF BIN images; AES-256-GCM keys: %u\r\n",
        SUF2_PRODUCT_ID_HEX, (unsigned long)SUF2_EXECUTION_BASE, (unsigned long)SUF2_SLOT_SIZE,
        (unsigned)SUF2_DECRYPTION_KEY_COUNT);
    return n < 0 ? size : (size_t)n < size ? (size_t)n : size - 1;
}

static size_t status_text(void *ctx, char *buf, size_t size)
{
    (void)ctx;
    xSemaphoreTake(lock, portMAX_DELAY);
    int n = snprintf(buf, size, "State: %s\r\nReceived: %lu of %lu blocks\r\nLast error: %s\r\n"
        "After a failed copy, eject the drive and copy again\r\n",
        installing ? "installing" : session.state == SUF2_STATE_FAILED ? "failed" : "ready/receiving",
        (unsigned long)session.received, (unsigned long)session.num_blocks, suf2_status_name(last_error));
    xSemaphoreGive(lock);
    return n < 0 ? size : (size_t)n < size ? (size_t)n : size - 1;
}

static void on_sector(void *ctx, const uint8_t sector[SUF2_BLOCK_SIZE])
{
    (void)ctx;
    xSemaphoreTake(lock, portMAX_DELAY);
    bool idle = session.state == SUF2_STATE_IDLE;
    suf2_status_t status;
    suf2_sector_result_t result = suf2_session_write(&session, sector, &status);
    if (idle && result == SUF2_SECTOR_STAGED) last_error = SUF2_OK;
    if (result == SUF2_SECTOR_REJECTED && session.state == SUF2_STATE_FAILED) last_error = session.error;
    if (result == SUF2_SECTOR_REJECTED) ESP_LOGW(TAG, "receiver: %s", suf2_status_name(status));
    xSemaphoreGive(lock);
}

static void on_unmount(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    if (!installing) {
        suf2_session_reset(&session);
        suf2_store_session_reset(&store);
    }
    xSemaphoreGive(lock);
}

void app_main(void)
{
    lock = xSemaphoreCreateMutex();
    if (!lock) { ESP_LOGE(TAG, "mutex allocation failed"); abort(); }
    check(suf2_esp32_store_init(&store, erased, sizeof erased));
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    suf2_meta_state_t meta;
    suf2_status_t status = suf2_store_read_meta(&store, manifest, &meta);
    if (status == SUF2_OK && meta == SUF2_META_PENDING) {
        status = suf2_store_install(&store, manifest, &policy);
        if (status == SUF2_OK) esp_restart();
    }
    if (status != SUF2_OK) {
        last_error = status;
        ESP_LOGE(TAG, "recovery: %s", suf2_status_name(status));
    }
    staging = suf2_store_staging(&store);
    const suf2_session_config_t config = {&staging, SUF2_SLOT_SIZE, blocks, sizeof blocks};
    check(suf2_session_init(&session, &config));
    const suf2_disk_config_t disk_config = {.info_text = info_text, .status_text = status_text, .on_sector = on_sector};
    check(suf2_disk_init(&disk, &disk_config));
    ESP_ERROR_CHECK(suf2_usb_start(&disk, on_unmount));
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreTake(lock, portMAX_DELAY);
        if (session.state == SUF2_STATE_COMPLETE && last_error == SUF2_OK) {
            installing = true;
            status = suf2_verify_candidate(&session, &policy);
            if (status == SUF2_OK) status = suf2_store_install(&store, session.manifest, &policy);
            if (status == SUF2_OK) esp_restart();
            last_error = status;
            session.state = SUF2_STATE_FAILED;
            session.error = status;
            installing = false;
            ESP_LOGE(TAG, "install: %s", suf2_status_name(status));
        }
        xSemaphoreGive(lock);
    }
}

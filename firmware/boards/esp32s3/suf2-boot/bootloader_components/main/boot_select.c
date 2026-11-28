#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "esp_log.h"
#include "flash_port.h"
#include "update_request.h"
#include "suf2_config.h"
#include "suf2_keys.h"

static const char *TAG = "suf2_boot";
static suf2_store_t store;
static uint8_t erased[(SUF2_SLOT_SIZE / SUF2_STORE_SECTOR_SIZE + 7u) / 8u];

static const suf2_policy_t policy = {
    .product_id = SUF2_PRODUCT_ID_BYTES, .execution_base = SUF2_EXECUTION_BASE,
    .max_image_size = SUF2_SLOT_SIZE, .keys = suf2_trusted_keys,
    .key_count = SUF2_TRUSTED_KEY_COUNT,
};

void __attribute__((noreturn)) call_start_cpu0(void)
{
    if (bootloader_init() != ESP_OK) bootloader_reset();
    bootloader_state_t partitions = {0};
    if (!bootloader_utility_load_partition_table(&partitions) ||
        partitions.factory.offset != SUF2_RECOVERY_OFFSET || partitions.factory.size != SUF2_RECOVERY_SIZE ||
        partitions.test.offset != SUF2_APP_OFFSET || partitions.test.size != SUF2_SLOT_SIZE) {
        ESP_LOGE(TAG, "partition layout does not match SUF2 policy");
        bootloader_reset();
    }
    bool requested = suf2_esp32_take_update_request();
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    suf2_meta_state_t state = SUF2_META_EMPTY;
    suf2_status_t status = suf2_esp32_store_init(&store, erased, sizeof erased);
    if (status == SUF2_OK) status = suf2_store_read_meta(&store, manifest, &state);
    bootloader_state_t allowed = {0};
    allowed.factory = partitions.factory;
    if (!requested && status == SUF2_OK && state == SUF2_META_COMMITTED) {
        suf2_reader_t app = suf2_store_app_reader(&store);
        status = suf2_verify_boot(manifest, &policy, &app);
        if (status == SUF2_OK) {
            suf2_manifest_t info = {0};
            status = suf2_manifest_parse(manifest, &info);
            esp_image_metadata_t image = {0};
            esp_partition_pos_t candidate = {.offset = SUF2_APP_OFFSET, .size = info.image_length};
            if (status == SUF2_OK && esp_image_verify(ESP_IMAGE_VERIFY, &candidate, &image) == ESP_OK) {
                allowed.factory = candidate;
                ESP_LOGI(TAG, "authenticated application selected");
            } else {
                ESP_LOGE(TAG, "signed payload is not a valid ESP-IDF application");
            }
        }
    }
    if (status != SUF2_OK) ESP_LOGE(TAG, "SUF2 boot check: %s", suf2_status_name(status));
    /* Only one approved partition is visible to IDF's fallback search. */
    bootloader_utility_load_boot_image(&allowed, FACTORY_INDEX);
}

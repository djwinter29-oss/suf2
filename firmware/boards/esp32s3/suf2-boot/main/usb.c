#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_private/usb_phy.h"
#include "tusb.h"
#include "usb.h"

static suf2_disk_t *drive;
static void (*on_unmount)(void);
static usb_phy_handle_t phy;
static char serial[13];

static const tusb_desc_device_t device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bMaxPacketSize0 = 64, .idVendor = 0x1209, .idProduct = 0x0001,
    .bcdDevice = 0x0100, .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1,
};
static const uint8_t configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN, 0, 100),
    TUD_MSC_DESCRIPTOR(0, 0, 0x01, 0x81, 64),
};

const uint8_t *tud_descriptor_device_cb(void) { return (const uint8_t *)&device; }
const uint8_t *tud_descriptor_configuration_cb(uint8_t index) { (void)index; return configuration; }

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t descriptor[32];
    const char *strings[] = {"", "SUF2 project", "SUF2 ESP32-S3 loader", serial};
    if (index >= sizeof strings / sizeof strings[0]) return NULL;
    size_t len;
    if (index == 0) {
        descriptor[1] = 0x0409;
        len = 1;
    } else {
        len = strlen(strings[index]);
        if (len > 31) len = 31;
        for (size_t i = 0; i < len; i++) descriptor[i + 1] = (uint8_t)strings[index][i];
    }
    descriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return descriptor;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor[8], uint8_t product[16], uint8_t revision[4])
{
    (void)lun;
    memcpy(vendor, "SUF2    ", 8); memcpy(product, "ESP32-S3 Loader ", 16); memcpy(revision, "1.0 ", 4);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { (void)lun; return true; }
void tud_msc_capacity_cb(uint8_t lun, uint32_t *count, uint16_t *size)
{
    (void)lun; *count = SUF2_DISK_SECTOR_COUNT; *size = SUF2_BLOCK_SIZE;
}
static int32_t result(uint8_t lun, suf2_status_t status, uint32_t size)
{
    if (status == SUF2_OK) return (int32_t)size;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0);
    return -1;
}
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    return result(lun, suf2_disk_read(drive, lba, offset, buffer, size), size);
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    (void)lba;
    return result(lun, suf2_disk_write(drive, offset, buffer, size), size);
}
int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t cmd[16], void *buffer, uint16_t size)
{
    (void)buffer; (void)size;
    if (cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0);
    return -1;
}
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool eject)
{
    (void)lun; (void)power_condition;
    if (!start && eject) on_unmount();
    return true;
}
void tud_umount_cb(void) { on_unmount(); }

static void usb_task(void *arg)
{
    (void)arg;
    for (;;) tud_task();
}

esp_err_t suf2_usb_start(suf2_disk_t *disk, void (*unmount)(void))
{
    drive = disk;
    on_unmount = unmount;
    uint8_t mac[6];
    esp_err_t status = esp_efuse_mac_get_default(mac);
    if (status != ESP_OK) return status;
    snprintf(serial, sizeof serial, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    const usb_phy_config_t config = {
        .controller = USB_PHY_CTRL_OTG, .target = USB_PHY_TARGET_INT, .otg_mode = USB_OTG_MODE_DEVICE,
    };
    status = usb_new_phy(&config, &phy);
    if (status != ESP_OK) return status;
    tusb_rhport_init_t port = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tusb_init(0, &port)) {
        usb_del_phy(phy);
        return ESP_FAIL;
    }
    if (xTaskCreate(usb_task, "suf2_usb", 4096, NULL, 5, NULL) != pdPASS) return ESP_ERR_NO_MEM;
    return ESP_OK;
}

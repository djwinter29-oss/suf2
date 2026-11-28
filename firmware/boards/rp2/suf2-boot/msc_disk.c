#include <string.h>

#include "tusb.h"
#include "loader.h"
#include "suf2/suf2_disk.h"

static suf2_disk_t disk;

static size_t info_text(void *ctx, char *buf, size_t size)
{
    (void)ctx;
    return loader_info_text(buf, size);
}

static size_t status_text(void *ctx, char *buf, size_t size)
{
    (void)ctx;
    return loader_status_text(buf, size);
}

static void on_sector(void *ctx, const uint8_t sector[SUF2_BLOCK_SIZE])
{
    (void)ctx;
    loader_on_sector(sector);
}

suf2_status_t msc_disk_init(void)
{
    const suf2_disk_config_t cfg = {
        .info_text = info_text, .status_text = status_text, .on_sector = on_sector,
    };
    return suf2_disk_init(&disk, &cfg);
}

static int32_t transfer_result(uint8_t lun, suf2_status_t status, uint32_t size)
{
    if (status == SUF2_OK) return (int32_t)size;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0x00);
    return -1;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id, "SUF2    ", 8);
    memcpy(product_id, "SUF2 Loader     ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = SUF2_DISK_SECTOR_COUNT;
    *block_size = SUF2_BLOCK_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    if (load_eject && !start) loader_on_unmount();
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    return transfer_result(lun, suf2_disk_read(&disk, lba, offset, buffer, size), size);
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    (void)lba;
    return transfer_result(lun, suf2_disk_write(&disk, offset, buffer, size), size);
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t cmd[16], void *buffer, uint16_t size)
{
    (void)buffer;
    (void)size;
    if (cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

void tud_umount_cb(void)
{
    loader_on_unmount();
}

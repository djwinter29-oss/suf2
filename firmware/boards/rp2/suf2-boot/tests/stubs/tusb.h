/* Host stub: just enough of TinyUSB for msc_disk.c. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1E
#define SCSI_SENSE_ILLEGAL_REQUEST 0x05
bool tud_msc_set_sense(uint8_t lun, uint8_t sense_key, uint8_t add_sense_code, uint8_t add_sense_qualifier);
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize);
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize);
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size);

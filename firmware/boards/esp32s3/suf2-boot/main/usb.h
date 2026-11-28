#pragma once

#include "esp_err.h"
#include "suf2/suf2_disk.h"

esp_err_t suf2_usb_start(suf2_disk_t *disk, void (*unmount)(void));

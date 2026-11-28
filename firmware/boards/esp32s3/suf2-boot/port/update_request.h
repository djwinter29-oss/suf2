#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "bootloader_common.h"

/* IDF reserves the same custom RTC FAST region in the bootloader and both apps. */
#define SUF2_ESP32_REQUEST_MAGIC 0x53554632u

static inline bool suf2_esp32_take_update_request(void)
{
    uint32_t words[2];
    uint8_t *custom = bootloader_common_get_rtc_retain_mem()->custom;
    memcpy(words, custom, sizeof words);
    bool requested = words[0] == SUF2_ESP32_REQUEST_MAGIC && words[1] == ~SUF2_ESP32_REQUEST_MAGIC;
    memset(custom, 0, sizeof words);
    return requested;
}

static inline void suf2_esp32_set_update_request(void)
{
    const uint32_t words[2] = {SUF2_ESP32_REQUEST_MAGIC, ~SUF2_ESP32_REQUEST_MAGIC};
    memcpy(bootloader_common_get_rtc_retain_mem()->custom, words, sizeof words);
}

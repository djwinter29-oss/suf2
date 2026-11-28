#pragma once

#include "suf2/suf2_store.h"

suf2_status_t suf2_esp32_store_init(suf2_store_t *store, uint8_t *bitmap, uint32_t bitmap_size);

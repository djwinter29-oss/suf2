/* Shared between the loader main loop and the USB drive. */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Called for every 512-byte sector the host writes. */
void loader_on_sector(const uint8_t sector[512]);

/* The host ejected or unmounted the drive. */
void loader_on_unmount(void);

/* Render the two text files shown on the drive. Return the length. */
size_t loader_info_text(char *buf, size_t size);
size_t loader_status_text(char *buf, size_t size);

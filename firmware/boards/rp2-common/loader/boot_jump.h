#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Sanity checks on the vector table at `vtor` (an XIP address). */
bool boot_vector_table_ok(uint32_t vtor, uint32_t slot_start, uint32_t slot_end);

/* Hands the CPU to the application. Call before any peripheral is set up. */
void boot_jump(uint32_t vtor) __attribute__((noreturn));

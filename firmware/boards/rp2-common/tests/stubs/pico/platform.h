#pragma once
#include <stdint.h>
extern uint8_t sim_flash[];
#define XIP_BASE ((uintptr_t)sim_flash)

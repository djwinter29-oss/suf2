cmake_minimum_required(VERSION 3.13)

# SUF2 loader and example application for rp2040.
#   cmake -S firmware/boards/rp2040 -B build/rp2040 -G Ninja
#   cmake --build build/rp2040
# See firmware/README.md for keys, product IDs and production builds.

set(PICO_PLATFORM rp2040 CACHE STRING "Pico SDK platform")
set(PICO_BOARD pico CACHE STRING "Pico SDK board header")
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../rp2-common/pico_sdk_import.cmake)
project(suf2_rp2040 C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

set(SUF2_BOARD_NAME rp2040)
set(SUF2_DEFAULT_FLASH_SIZE 0x200000)
set(SUF2_APP_VECTOR_OFFSET 0x100)
set(SUF2_DEFAULT_PRODUCT_NAME SUF2RP2040)
include(${CMAKE_CURRENT_LIST_DIR}/../rp2-common/suf2_rp2.cmake)

suf2_rp2_add_loader(suf2_loader)
add_subdirectory(${SUF2_RP2_DIR}/examples/blink blink)

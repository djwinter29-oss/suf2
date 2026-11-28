cmake_minimum_required(VERSION 3.13)

# SUF2 core library. Use it in two ways:
#   - add_subdirectory() from a firmware build (the RP2040/RP2350 loaders);
#   - configure this folder directly for a native build with tests:
#       cmake -S firmware/suf2 -B build/suf2-native && cmake --build build/suf2-native
#       ctest --test-dir build/suf2-native --output-on-failure

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(suf2_core C)
    set(SUF2_STANDALONE ON)
    set(CMAKE_C_STANDARD 11)
    set(CMAKE_C_STANDARD_REQUIRED ON)
endif()

add_library(suf2 STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/suf2.c
    ${CMAKE_CURRENT_LIST_DIR}/src/suf2_store.c
    ${CMAKE_CURRENT_LIST_DIR}/src/suf2_disk.c)
target_include_directories(suf2 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(suf2 PRIVATE -ffunction-sections -fdata-sections)
endif()

if (SUF2_STANDALONE)
    target_compile_options(suf2 PRIVATE -Wall -Wextra -Werror)
    enable_testing()
    add_subdirectory(tests)
endif()

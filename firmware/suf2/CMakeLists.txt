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

set(SUF2_MBEDTLS_DIR "${CMAKE_CURRENT_LIST_DIR}/../lib/mbedtls" CACHE PATH "Mbed TLS source tree (git submodule)")
option(SUF2_CRYPTO_MBEDTLS "Build the default Mbed TLS crypto backend" ON)

if (NOT EXISTS "${SUF2_MBEDTLS_DIR}/library/ecdsa.c" AND SUF2_CRYPTO_MBEDTLS)
    message(FATAL_ERROR "Mbed TLS not found at ${SUF2_MBEDTLS_DIR}. Run: git submodule update --init firmware/lib/mbedtls")
endif()

add_library(suf2 STATIC ${CMAKE_CURRENT_LIST_DIR}/src/suf2.c)
target_include_directories(suf2 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

if (SUF2_CRYPTO_MBEDTLS)
    set(_mbedtls_sources asn1parse.c asn1write.c bignum.c bignum_core.c constant_time.c
        ecdsa.c ecp.c ecp_curves.c platform_util.c sha256.c)
    list(TRANSFORM _mbedtls_sources PREPEND "${SUF2_MBEDTLS_DIR}/library/")
    add_library(suf2_mbedtls STATIC ${_mbedtls_sources})
    target_include_directories(suf2_mbedtls
        PUBLIC ${SUF2_MBEDTLS_DIR}/include ${CMAKE_CURRENT_LIST_DIR}/port
        PRIVATE ${SUF2_MBEDTLS_DIR}/library)
    target_compile_definitions(suf2_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="suf2_mbedtls_config.h")
    target_sources(suf2 PRIVATE ${CMAKE_CURRENT_LIST_DIR}/port/suf2_crypto_mbedtls.c)
    target_link_libraries(suf2 PUBLIC suf2_mbedtls)
endif()

# Let the linker drop unused code; Mbed TLS brings far more than verification needs.
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(suf2 PRIVATE -ffunction-sections -fdata-sections)
    if (TARGET suf2_mbedtls)
        target_compile_options(suf2_mbedtls PRIVATE -ffunction-sections -fdata-sections)
    endif()
endif()

if (SUF2_STANDALONE)
    target_compile_options(suf2 PRIVATE -Wall -Wextra -Werror)
    enable_testing()
    add_subdirectory(tests)
endif()

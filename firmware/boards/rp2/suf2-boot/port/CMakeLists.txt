get_filename_component(_mbedtls_dir "${CMAKE_CURRENT_LIST_DIR}/../../../../lib/mbedtls" ABSOLUTE)
set(SUF2_MBEDTLS_DIR "${_mbedtls_dir}"
    CACHE PATH "Mbed TLS source tree (git submodule)")
if (NOT EXISTS "${SUF2_MBEDTLS_DIR}/library/ecdsa.c")
    message(FATAL_ERROR "Mbed TLS not found at ${SUF2_MBEDTLS_DIR}. Run: git submodule update --init firmware/lib/mbedtls")
endif()

set(_mbedtls_sources asn1parse.c asn1write.c bignum.c bignum_core.c constant_time.c
    ecdsa.c ecp.c ecp_curves.c platform_util.c sha256.c aes.c cipher.c cipher_wrap.c gcm.c)
list(TRANSFORM _mbedtls_sources PREPEND "${SUF2_MBEDTLS_DIR}/library/")
add_library(suf2_mbedtls STATIC ${_mbedtls_sources})
target_include_directories(suf2_mbedtls
    PUBLIC ${SUF2_MBEDTLS_DIR}/include ${CMAKE_CURRENT_LIST_DIR}
    PRIVATE ${SUF2_MBEDTLS_DIR}/library)
target_compile_definitions(suf2_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="suf2_mbedtls_config.h")

add_library(suf2_crypto_mbedtls STATIC suf2_crypto_mbedtls.c)
target_include_directories(suf2_crypto_mbedtls PRIVATE
    "$<TARGET_PROPERTY:suf2,INTERFACE_INCLUDE_DIRECTORIES>")
target_link_libraries(suf2_crypto_mbedtls PRIVATE suf2_mbedtls)
if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(suf2_crypto_mbedtls PRIVATE -ffunction-sections -fdata-sections)
    target_compile_options(suf2_mbedtls PRIVATE -ffunction-sections -fdata-sections)
endif()

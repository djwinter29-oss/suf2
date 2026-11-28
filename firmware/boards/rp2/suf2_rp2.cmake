# Shared build logic for the SUF2 loader on RP2040 and RP2350.
#
# A board CMakeLists.txt sets these, then includes this file after
# pico_sdk_init():
#   SUF2_BOARD_NAME             e.g. rp2040
#   SUF2_DEFAULT_FLASH_SIZE     flash size of the default board, in bytes
#   SUF2_APP_VECTOR_OFFSET      vector table offset inside the app slot
#   SUF2_DEFAULT_PRODUCT_NAME   ASCII product ID used for development builds
#
# It provides:
#   suf2_rp2_add_loader(TARGET)  the loader executable
#   suf2_rp2_add_app(TARGET)     links an application for the app slot and,
#                                when SUF2_SIGNING_KEY is set, packs it into
#                                TARGET.suf2

include_guard(GLOBAL)

set(SUF2_RP2_DIR ${CMAKE_CURRENT_LIST_DIR})
get_filename_component(SUF2_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." REALPATH)
set(SUF2_LOADER_VERSION "0.1.0")

# ---- Options ---------------------------------------------------------------

set(SUF2_FLASH_SIZE ${SUF2_DEFAULT_FLASH_SIZE} CACHE STRING "Flash size in bytes")
set(SUF2_LOADER_SIZE 0x10000 CACHE STRING "Flash reserved for the loader, in bytes (multiple of 4096)")
option(SUF2_PRODUCTION "Refuse test keys and the default product ID" OFF)
set(SUF2_PRODUCT_ID "" CACHE STRING "Product ID as 32 hex digits (default: ASCII ${SUF2_DEFAULT_PRODUCT_NAME})")
set(SUF2_TRUSTED_KEYS "" CACHE STRING "Semicolon-separated PEM public keys or certificates the loader trusts")
set(SUF2_SIGNING_KEY "" CACHE FILEPATH "Private key used to pack example apps at build time (development only)")
set(SUF2_USB_VID 0x1209 CACHE STRING "USB vendor ID (default: pid.codes test VID)")
set(SUF2_USB_PID 0x0001 CACHE STRING "USB product ID (default: pid.codes test PID)")
set(SUF2_USB_MANUFACTURER "SUF2 project" CACHE STRING "USB manufacturer string")
set(SUF2_USB_PRODUCT "SUF2 loader (${SUF2_BOARD_NAME})" CACHE STRING "USB product string")
option(SUF2_LOADER_UF2 "Also produce loader.uf2 (the SDK fetches and builds picotool)" ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# ---- Flash layout ----------------------------------------------------------
#
#   [0, L)            loader
#   [L, L+4K)         META: install journal and commit record
#   [L+4K, L+4K+S)    application slot
#   [end-S, end)      staging, the same size as the slot

math(EXPR _rem "${SUF2_LOADER_SIZE} % 4096")
if (NOT _rem EQUAL 0)
    message(FATAL_ERROR "SUF2_LOADER_SIZE must be a multiple of 4096")
endif()
math(EXPR SUF2_META_OFFSET "${SUF2_LOADER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR SUF2_APP_OFFSET "${SUF2_LOADER_SIZE} + 4096" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR SUF2_SLOT_SIZE "(${SUF2_FLASH_SIZE} - ${SUF2_APP_OFFSET}) / 2 / 4096 * 4096" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR SUF2_STAGING_OFFSET "${SUF2_FLASH_SIZE} - ${SUF2_SLOT_SIZE}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR SUF2_APP_BASE "0x10000000 + ${SUF2_APP_OFFSET}" OUTPUT_FORMAT HEXADECIMAL)
math(EXPR SUF2_SLOT_KB "${SUF2_SLOT_SIZE} / 1024")
message(STATUS "SUF2 ${SUF2_BOARD_NAME}: loader ${SUF2_LOADER_SIZE}, app ${SUF2_APP_BASE} (${SUF2_SLOT_KB} KB), "
    "staging at flash offset ${SUF2_STAGING_OFFSET}")

# ---- Product ID ------------------------------------------------------------

string(HEX "${SUF2_DEFAULT_PRODUCT_NAME}" _default_pid)
string(LENGTH "${_default_pid}" _len)
while (_len LESS 32)
    string(APPEND _default_pid "0")
    string(LENGTH "${_default_pid}" _len)
endwhile()
if (SUF2_PRODUCT_ID)
    set(_pid "${SUF2_PRODUCT_ID}")
else()
    set(_pid "${_default_pid}")
endif()
string(TOLOWER "${_pid}" _pid)
if (NOT _pid MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "SUF2_PRODUCT_ID must be exactly 32 hex digits")
endif()
string(LENGTH "${_pid}" _len)
if (NOT _len EQUAL 32)
    message(FATAL_ERROR "SUF2_PRODUCT_ID must be exactly 32 hex digits")
endif()
set(SUF2_PRODUCT_ID_HEX "${_pid}")
string(REGEX REPLACE "(..)" "0x\\1, " SUF2_PRODUCT_ID_C "${_pid}")
set(SUF2_PRODUCT_ID_C "{${SUF2_PRODUCT_ID_C}}")

# ---- Trusted keys ----------------------------------------------------------

set(_shared "${SUF2_REPO_DIR}/host/shared")
set(_keys ${SUF2_TRUSTED_KEYS})
if (SUF2_PRODUCTION)
    if (NOT _keys)
        message(FATAL_ERROR "SUF2_PRODUCTION needs SUF2_TRUSTED_KEYS (your release public keys)")
    endif()
    if (_pid STREQUAL _default_pid)
        message(FATAL_ERROR "SUF2_PRODUCTION needs your own SUF2_PRODUCT_ID, not the development default")
    endif()
elseif (NOT _keys)
    if (EXISTS "${_shared}/local/signing-public.pem")
        set(_keys "${_shared}/local/signing-public.pem")
        message(STATUS "SUF2: trusting the local test key ${_keys}")
    else()
        set(_keys "${_shared}/examples/signing-public.pem")
        message(WARNING "SUF2: trusting the example public key, whose private key is not published. "
            "Nothing can be signed for this loader. Run host/shared/generate-test-keys.py and reconfigure.")
    endif()
endif()

set(SUF2_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(_keys_args)
if (SUF2_PRODUCTION)
    list(APPEND _keys_args --production --forbid-dir "${_shared}")
endif()
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${SUF2_REPO_DIR}/tools/firmware/suf2_keys_header.py"
            --output "${SUF2_GENERATED_DIR}/suf2_keys.h" ${_keys_args} ${_keys}
    RESULT_VARIABLE _rc ERROR_VARIABLE _err)
if (NOT _rc EQUAL 0)
    message(FATAL_ERROR "SUF2: could not build the trusted key table:\n${_err}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SUF2_REPO_DIR}/tools/firmware/suf2_keys_header.py" ${_keys})

configure_file("${SUF2_RP2_DIR}/suf2-boot/suf2_board_config.h.in" "${SUF2_GENERATED_DIR}/suf2_board_config.h")

# ---- Libraries -------------------------------------------------------------

add_subdirectory("${SUF2_REPO_DIR}/firmware/suf2" "${CMAKE_BINARY_DIR}/suf2")
option(SUF2_CRYPTO_MBEDTLS "Use the RP2 example's Mbed TLS crypto port" ON)
if (SUF2_CRYPTO_MBEDTLS)
    add_subdirectory("${SUF2_RP2_DIR}/suf2-boot/port" "${CMAKE_BINARY_DIR}/suf2-port")
    target_link_libraries(suf2 PUBLIC suf2_crypto_mbedtls)
endif()

add_library(suf2_app INTERFACE)
target_sources(suf2_app INTERFACE "${SUF2_RP2_DIR}/suf2-boot/app/suf2_app.c")
target_include_directories(suf2_app INTERFACE "${SUF2_RP2_DIR}/suf2-boot/app")
target_link_libraries(suf2_app INTERFACE hardware_watchdog hardware_sync)

# Point the linker's FLASH region at [ORIGIN, ORIGIN + LENGTH).
function(suf2_rp2_flash_region TARGET ORIGIN LENGTH)
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_ld")
    file(CONFIGURE OUTPUT "${dir}/pico_flash_region.ld"
        CONTENT "FLASH(rx) : ORIGIN = ${ORIGIN}, LENGTH = ${LENGTH}\n")
    pico_add_linker_script_override_path(${TARGET} "${dir}" FILES pico_flash_region.ld)
    target_compile_definitions(${TARGET} PRIVATE PICO_FLASH_SIZE_BYTES=${SUF2_FLASH_SIZE})
endfunction()

function(suf2_rp2_add_loader TARGET)
    set(src "${SUF2_RP2_DIR}/suf2-boot")
    add_executable(${TARGET}
        ${src}/loader_main.c ${src}/flash_store.c ${src}/boot_jump.c
        ${src}/msc_disk.c ${src}/usb_descriptors.c)
    # Keep loader artifacts at build/<board>/ for the build/flash tools.
    set_target_properties(${TARGET} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    target_include_directories(${TARGET} PRIVATE ${src} "${SUF2_RP2_DIR}/suf2-boot/app" "${SUF2_GENERATED_DIR}")
    target_link_libraries(${TARGET} PRIVATE
        pico_stdlib pico_unique_id hardware_flash hardware_watchdog tinyusb_device suf2)
    target_compile_options(${TARGET} PRIVATE -Wall -Wextra)
    # The status files only print integers.
    target_compile_definitions(${TARGET} PRIVATE
        PICO_PRINTF_SUPPORT_FLOAT=0 PICO_PRINTF_SUPPORT_EXPONENTIAL=0 PICO_PRINTF_SUPPORT_LONG_LONG=0)
    pico_enable_stdio_uart(${TARGET} 0)
    pico_enable_stdio_usb(${TARGET} 0)
    pico_set_program_name(${TARGET} "SUF2 loader")
    pico_set_program_version(${TARGET} "${SUF2_LOADER_VERSION}")
    suf2_rp2_flash_region(${TARGET} 0x10000000 ${SUF2_LOADER_SIZE})
    pico_add_hex_output(${TARGET})
    pico_add_bin_output(${TARGET})
    pico_add_dis_output(${TARGET})
    pico_add_map_output(${TARGET})
    if (SUF2_LOADER_UF2)
        pico_add_uf2_output(${TARGET})
    endif()
endfunction()

function(suf2_rp2_add_app TARGET)
    target_link_libraries(${TARGET} PRIVATE suf2_app)
    suf2_rp2_flash_region(${TARGET} ${SUF2_APP_BASE} ${SUF2_SLOT_SIZE})
    pico_add_dis_output(${TARGET})
    pico_add_map_output(${TARGET})
    if (SUF2_SIGNING_KEY)
        if (SUF2_PRODUCTION)
            message(FATAL_ERROR "Do not pack with SUF2_SIGNING_KEY in a production build; sign releases offline")
        endif()
        set(out "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.suf2")
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -f "${out}"
            COMMAND ${CMAKE_COMMAND} -E env "PYTHONPATH=${SUF2_REPO_DIR}/host/python/src"
                ${Python3_EXECUTABLE} -m suf2 pack
                --input "$<TARGET_FILE:${TARGET}>" --output "${out}" --key "${SUF2_SIGNING_KEY}"
                --product-id ${SUF2_PRODUCT_ID_HEX} --base ${SUF2_APP_BASE}
            COMMENT "Packing ${TARGET}.suf2"
            VERBATIM)
    endif()
endfunction()

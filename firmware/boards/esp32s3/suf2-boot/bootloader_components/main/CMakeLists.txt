get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." REALPATH)
get_filename_component(_generated "${SDKCONFIG}" DIRECTORY)
set(_port "${CMAKE_CURRENT_LIST_DIR}/../../port")
idf_component_register(
    SRCS "boot_select.c" "${_port}/crypto_boot.c" "${_port}/flash_port.c"
        "${_repo}/firmware/suf2/src/suf2.c" "${_repo}/firmware/suf2/src/suf2_store.c"
    INCLUDE_DIRS "${_repo}/firmware/suf2/include" "${_port}" "${_generated}/generated"
    REQUIRES bootloader bootloader_support micro-ecc spi_flash)
idf_build_get_property(_scripts BOOTLOADER_LINKER_SCRIPT)
target_linker_script(${COMPONENT_LIB} INTERFACE "${_scripts}")

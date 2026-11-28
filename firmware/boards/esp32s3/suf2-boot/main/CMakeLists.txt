get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../../../../.." REALPATH)
set(_core "${_repo}/firmware/suf2")
set(_usb "${_repo}/firmware/lib/tinyusb/src")
idf_component_register(
    SRCS "loader_main.c" "usb.c" "../port/flash_port.c"
        "${_core}/src/suf2.c" "${_core}/src/suf2_store.c" "${_core}/src/suf2_disk.c"
        "${_repo}/firmware/boards/rp2/suf2-boot/port/suf2_crypto_mbedtls.c"
        "${_usb}/tusb.c" "${_usb}/common/tusb_fifo.c" "${_usb}/device/usbd.c"
        "${_usb}/device/usbd_control.c" "${_usb}/class/msc/msc_device.c"
        "${_usb}/portable/espressif/esp32sx/dcd_esp32sx.c"
    INCLUDE_DIRS "." "../port" "${_core}/include" "${_usb}" "${CMAKE_BINARY_DIR}/generated"
    REQUIRES esp_system spi_flash mbedtls usb esp_hw_support)
target_compile_definitions(${COMPONENT_LIB} PUBLIC SUF2_SHA256_CTX_SIZE=256)

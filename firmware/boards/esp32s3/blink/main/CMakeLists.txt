idf_component_register(SRCS "blink.c" INCLUDE_DIRS "../../suf2-boot/port"
    REQUIRES esp_driver_gpio esp_system bootloader_support)

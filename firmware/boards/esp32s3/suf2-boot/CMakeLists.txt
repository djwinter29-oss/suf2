cmake_minimum_required(VERSION 3.16)
include(../suf2_esp32s3.cmake)
include(../esp_idf_import.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(suf2_boot)

suf2_esp32_validate_config()

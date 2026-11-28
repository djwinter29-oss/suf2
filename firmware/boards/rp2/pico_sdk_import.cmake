# Use the pico-sdk submodule unless the caller points PICO_SDK_PATH elsewhere.
# Must be included before project().

if (DEFINED ENV{PICO_SDK_PATH} AND NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if (NOT PICO_SDK_PATH)
    get_filename_component(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/../../lib/pico-sdk" REALPATH)
endif()
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK")

if (NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "pico-sdk not found at ${PICO_SDK_PATH}.\n"
        "Run: git submodule update --init firmware/lib/pico-sdk && "
        "git -C firmware/lib/pico-sdk submodule update --init lib/tinyusb")
endif()
if (NOT EXISTS "${PICO_SDK_PATH}/lib/tinyusb/src/tusb.h")
    message(FATAL_ERROR "TinyUSB is missing. Run: git -C firmware/lib/pico-sdk submodule update --init lib/tinyusb")
endif()

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

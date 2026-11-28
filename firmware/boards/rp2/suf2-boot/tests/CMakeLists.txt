cmake_minimum_required(VERSION 3.13)

# Host simulation of the RP2 loader's flash and USB drive code.
#   cmake -S firmware/boards/rp2/suf2-boot/tests -B build/rp2-sim
#   cmake --build build/rp2-sim && ctest --test-dir build/rp2-sim --output-on-failure

project(suf2_rp2_sim C)
set(CMAKE_C_STANDARD 11)
enable_testing()

get_filename_component(SUF2_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../.." REALPATH)
add_subdirectory("${SUF2_REPO_DIR}/firmware/suf2" suf2)
add_subdirectory(../port suf2-port)
target_link_libraries(suf2 PUBLIC suf2_crypto_mbedtls)

# The default RP2040 layout: 2 MB flash, 64 KB loader.
set(SUF2_BOARD_NAME sim)
set(SUF2_LOADER_VERSION sim)
set(SUF2_FLASH_SIZE 0x200000)
set(SUF2_LOADER_SIZE 0x10000)
set(SUF2_META_OFFSET 0x10000)
set(SUF2_APP_OFFSET 0x11000)
set(SUF2_SLOT_SIZE 0xF7000)
set(SUF2_STAGING_OFFSET 0x109000)
set(SUF2_APP_VECTOR_OFFSET 0x100)
set(SUF2_PRODUCT_ID_C "{0}")
set(SUF2_USB_VID 0x1209)
set(SUF2_USB_PID 0x0001)
set(SUF2_PRODUCTION OFF)
configure_file(../suf2_board_config.h.in generated/suf2_board_config.h)

set(loader ${CMAKE_CURRENT_LIST_DIR}/..)
add_executable(sim_loader sim_loader.c ${loader}/flash_store.c ${loader}/msc_disk.c)
target_include_directories(sim_loader PRIVATE stubs ${loader} ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(sim_loader PRIVATE suf2)
target_compile_options(sim_loader PRIVATE -Wall -Wextra -Werror -fsanitize=address,undefined)
target_link_options(sim_loader PRIVATE -fsanitize=address,undefined)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME rp2_loader_sim
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_loader_sim
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
set_tests_properties(rp2_loader_sim PROPERTIES
    ENVIRONMENT "SUF2_SIM_LOADER=$<TARGET_FILE:sim_loader>;PYTHONPATH=${SUF2_REPO_DIR}/host/python/src")

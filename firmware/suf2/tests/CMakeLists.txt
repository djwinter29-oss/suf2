add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../boards/rp2/suf2-boot/port"
    "${CMAKE_CURRENT_BINARY_DIR}/suf2-port")
target_link_libraries(suf2 PUBLIC suf2_crypto_mbedtls)

add_executable(suf2_harness suf2_harness.c)
target_link_libraries(suf2_harness PRIVATE suf2)
target_compile_options(suf2_harness PRIVATE -Wall -Wextra -Werror)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME suf2_core
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_suf2_core.py $<TARGET_FILE:suf2_harness>)
set_tests_properties(suf2_core PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/../../../host/python/src")

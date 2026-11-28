# Optional AES provisioning shared by the board recovery examples.
set(_aes_default "")
if (NOT SUF2_PRODUCTION AND EXISTS "${SUF2_REPO_DIR}/test-keys/local/encryption-key.jwk")
    set(_aes_default "${SUF2_REPO_DIR}/test-keys/local/encryption-key.jwk")
endif()
set(SUF2_DECRYPTION_KEYS "${_aes_default}" CACHE STRING "Semicolon-separated AES-256 octet JWK files (empty disables decryption keys)")
set(_aes_args)
if (SUF2_PRODUCTION)
    list(APPEND _aes_args --production
        --forbid-dir "${SUF2_REPO_DIR}/test-keys" --forbid-dir "${SUF2_REPO_DIR}/host/shared")
endif()
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${SUF2_REPO_DIR}/tools/firmware/suf2_aes_keys_header.py"
        --output "${SUF2_GENERATED_DIR}/suf2_aes_keys.h" ${_aes_args} ${SUF2_DECRYPTION_KEYS}
    RESULT_VARIABLE _aes_result ERROR_VARIABLE _aes_error)
if (NOT _aes_result EQUAL 0)
    message(FATAL_ERROR "Could not generate AES keys:\n${_aes_error}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${SUF2_REPO_DIR}/tools/firmware/suf2_aes_keys_header.py"
    "${SUF2_REPO_DIR}/tools/firmware/suf2_keys_header.py" ${SUF2_DECRYPTION_KEYS})

# RP2 crypto port

This is the board example's Mbed TLS implementation of the portable
[crypto contract](../../../../suf2/include/suf2/suf2_crypto.h).
The public application API and storage callbacks remain in
[suf2.h](../../../../suf2/include/suf2/suf2.h).

`suf2_crypto_mbedtls.c` implements SHA-256, P-256 verification and streaming
AES-256-GCM decryption with constant-time tag comparison.
`suf2_mbedtls_config.h` selects the minimal verification/decryption Mbed TLS build.
The CMake project produces `suf2_crypto_mbedtls` and `suf2_mbedtls` targets;
the board integration links the crypto target to `suf2`.
Native verifier tests and RP2 simulation tests reuse this port.

For a different platform, implement the functions declared in
`suf2_crypto.h`, provide the storage callbacks declared in `suf2.h`, and link
your backend through the firmware build. Do not put implementation files or
platform headers in `firmware/suf2/include/`.

To replace the crypto port in the RP2 example, configure with
`-DSUF2_CRYPTO_MBEDTLS=OFF` and link your implementation. Disabling the port
alone is not a working build: the linker must resolve all crypto functions.

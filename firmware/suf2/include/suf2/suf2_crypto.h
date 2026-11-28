/*
 * Porting contract used by the SUF2 core. The firmware integration must link
 * one implementation of these functions. The RP2 board example supplies an
 * Mbed TLS implementation in firmware/boards/rp2/suf2-boot/port, also reused by
 * the ESP32-S3 recovery app. Its second-stage bootloader supplies ROM SHA and
 * micro-ecc verification. Platforms may replace the backend without changing
 * this interface.
 */
#ifndef SUF2_SUF2_CRYPTO_H
#define SUF2_SUF2_CRYPTO_H

#include <stdint.h>

#include "suf2/suf2.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SUF2_SHA256_CTX_SIZE
#define SUF2_SHA256_CTX_SIZE 128u
#endif

typedef struct {
    union {
        uint64_t align;
        uint8_t bytes[SUF2_SHA256_CTX_SIZE];
    } u;
} suf2_sha256_t;

#ifndef SUF2_GCM_CTX_SIZE
#define SUF2_GCM_CTX_SIZE 512u
#endif

typedef struct suf2_gcm {
    union {
        uint64_t align;
        uint8_t bytes[SUF2_GCM_CTX_SIZE];
    } u;
} suf2_gcm_t;

suf2_status_t suf2_sha256_start(suf2_sha256_t *ctx);
suf2_status_t suf2_sha256_update(suf2_sha256_t *ctx, const uint8_t *data, uint32_t len);
/* Writes the digest and releases the context, also on failure. */
suf2_status_t suf2_sha256_finish(suf2_sha256_t *ctx, uint8_t digest[SUF2_DIGEST_SIZE]);

/*
 * ECDSA P-256 verification of a precomputed SHA-256 `hash` (not a message).
 * The core has already range-checked r and s. Returns SUF2_OK,
 * SUF2_E_SIGNATURE, or SUF2_E_CRYPTO_BACKEND for an invalid public key or a
 * library failure.
 */
suf2_status_t suf2_p256_verify(const uint8_t public_key[SUF2_PUBLIC_KEY_SIZE],
                               const uint8_t hash[SUF2_DIGEST_SIZE],
                               const uint8_t signature[SUF2_SIGNATURE_SIZE]);

/* Streaming AES-256-GCM decryption. Output is unauthenticated until finish.
 * update may buffer up to 15 bytes; provide input length + 15 bytes of space.
 * finish releases the context on all paths and returns SUF2_E_GCM_TAG for a bad tag.
 */
suf2_status_t suf2_gcm_start(suf2_gcm_t *ctx, const uint8_t key[SUF2_AES_KEY_SIZE],
                             const uint8_t nonce[12], const uint8_t *aad, uint32_t aad_len);
suf2_status_t suf2_gcm_update(suf2_gcm_t *ctx, const uint8_t *input, uint32_t len,
                              uint8_t *output, uint32_t capacity, uint32_t *written);
suf2_status_t suf2_gcm_finish(suf2_gcm_t *ctx, const uint8_t tag[16], uint8_t output[16],
                              uint32_t *written);


#ifdef __cplusplus
}
#endif

#endif

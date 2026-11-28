#include <string.h>

#include "bootloader_sha.h"
#include "uECC.h"
#include "uECC_verify_antifault.h"
#include "suf2/suf2_crypto.h"

suf2_status_t suf2_sha256_start(suf2_sha256_t *ctx)
{
    bootloader_sha256_handle_t handle = bootloader_sha256_start();
    if (!handle) return SUF2_E_CRYPTO_BACKEND;
    memcpy(ctx->u.bytes, &handle, sizeof handle);
    return SUF2_OK;
}

static bootloader_sha256_handle_t handle_of(const suf2_sha256_t *ctx)
{
    bootloader_sha256_handle_t handle;
    memcpy(&handle, ctx->u.bytes, sizeof handle);
    return handle;
}

suf2_status_t suf2_sha256_update(suf2_sha256_t *ctx, const uint8_t *data, uint32_t len)
{
    bootloader_sha256_data(handle_of(ctx), data, len);
    return SUF2_OK;
}

suf2_status_t suf2_sha256_finish(suf2_sha256_t *ctx, uint8_t digest[SUF2_DIGEST_SIZE])
{
    bootloader_sha256_finish(handle_of(ctx), digest);
    memset(ctx, 0, sizeof *ctx);
    return SUF2_OK;
}

suf2_status_t suf2_p256_verify(const uint8_t key[SUF2_PUBLIC_KEY_SIZE],
                               const uint8_t hash[SUF2_DIGEST_SIZE],
                               const uint8_t signature[SUF2_SIGNATURE_SIZE])
{
    if (key[0] != 4 || !uECC_valid_public_key(key + 1, uECC_secp256r1())) return SUF2_E_CRYPTO_BACKEND;
    uint8_t verified[SUF2_DIGEST_SIZE] = {0};
    if (uECC_verify_antifault(key + 1, hash, SUF2_DIGEST_SIZE, signature, uECC_secp256r1(), verified) != 1 ||
        memcmp(hash, verified, sizeof verified) != 0) return SUF2_E_SIGNATURE;
    return SUF2_OK;
}

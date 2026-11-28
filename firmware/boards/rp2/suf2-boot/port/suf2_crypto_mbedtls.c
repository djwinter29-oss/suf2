/* Default SUF2 crypto backend using Mbed TLS (SHA-256 and ECDSA P-256). */
#include <string.h>

#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/gcm.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "suf2/suf2_crypto.h"

_Static_assert(sizeof(mbedtls_sha256_context) <= SUF2_SHA256_CTX_SIZE, "SUF2_SHA256_CTX_SIZE too small");
_Static_assert(sizeof(mbedtls_gcm_context) <= SUF2_GCM_CTX_SIZE, "SUF2_GCM_CTX_SIZE too small");
_Static_assert(_Alignof(mbedtls_gcm_context) <= _Alignof(suf2_gcm_t), "SUF2 GCM context alignment too small");

static mbedtls_sha256_context *ctx_of(suf2_sha256_t *ctx)
{
    return (mbedtls_sha256_context *)(void *)ctx->u.bytes;
}

suf2_status_t suf2_sha256_start(suf2_sha256_t *ctx)
{
    mbedtls_sha256_init(ctx_of(ctx));
    if (mbedtls_sha256_starts(ctx_of(ctx), 0) != 0) {
        mbedtls_sha256_free(ctx_of(ctx));
        return SUF2_E_CRYPTO_BACKEND;
    }
    return SUF2_OK;
}

suf2_status_t suf2_sha256_update(suf2_sha256_t *ctx, const uint8_t *data, uint32_t len)
{
    return mbedtls_sha256_update(ctx_of(ctx), data, len) == 0 ? SUF2_OK : SUF2_E_CRYPTO_BACKEND;
}

suf2_status_t suf2_sha256_finish(suf2_sha256_t *ctx, uint8_t digest[SUF2_DIGEST_SIZE])
{
    int rc = mbedtls_sha256_finish(ctx_of(ctx), digest);
    mbedtls_sha256_free(ctx_of(ctx));
    return rc == 0 ? SUF2_OK : SUF2_E_CRYPTO_BACKEND;
}

suf2_status_t suf2_p256_verify(const uint8_t public_key[SUF2_PUBLIC_KEY_SIZE], const uint8_t hash[SUF2_DIGEST_SIZE],
                               const uint8_t signature[SUF2_SIGNATURE_SIZE])
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point q;
    mbedtls_mpi r, s;
    suf2_status_t st = SUF2_E_CRYPTO_BACKEND;

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&q);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    if (public_key[0] == 0x04 && mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(&grp, &q, public_key, SUF2_PUBLIC_KEY_SIZE) == 0 &&
        mbedtls_ecp_check_pubkey(&grp, &q) == 0 && mbedtls_mpi_read_binary(&r, signature, 32) == 0 &&
        mbedtls_mpi_read_binary(&s, signature + 32, 32) == 0) {
        int rc = mbedtls_ecdsa_verify(&grp, hash, SUF2_DIGEST_SIZE, &q, &r, &s);
        if (rc == 0)
            st = SUF2_OK;
        else if (rc == MBEDTLS_ERR_ECP_VERIFY_FAILED)
            st = SUF2_E_SIGNATURE;
    }

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&q);
    mbedtls_ecp_group_free(&grp);
    return st;
}

static mbedtls_gcm_context *gcm_of(suf2_gcm_t *ctx)
{
    return (mbedtls_gcm_context *)(void *)ctx->u.bytes;
}

suf2_status_t suf2_gcm_start(suf2_gcm_t *ctx, const uint8_t key[SUF2_AES_KEY_SIZE],
                             const uint8_t nonce[12], const uint8_t *aad, uint32_t aad_len)
{
    mbedtls_gcm_init(gcm_of(ctx));
    if (mbedtls_gcm_setkey(gcm_of(ctx), MBEDTLS_CIPHER_ID_AES, key, 256) != 0 ||
        mbedtls_gcm_starts(gcm_of(ctx), MBEDTLS_GCM_DECRYPT, nonce, 12) != 0 ||
        mbedtls_gcm_update_ad(gcm_of(ctx), aad, aad_len) != 0) {
        mbedtls_gcm_free(gcm_of(ctx));
        return SUF2_E_CRYPTO_BACKEND;
    }
    return SUF2_OK;
}

suf2_status_t suf2_gcm_update(suf2_gcm_t *ctx, const uint8_t *input, uint32_t len,
                              uint8_t *output, uint32_t capacity, uint32_t *written)
{
    size_t n = 0;
    *written = 0;
    if (mbedtls_gcm_update(gcm_of(ctx), input, len, output, capacity, &n) != 0 || n > capacity)
        return SUF2_E_CRYPTO_BACKEND;
    *written = (uint32_t)n;
    return SUF2_OK;
}

suf2_status_t suf2_gcm_finish(suf2_gcm_t *ctx, const uint8_t expected[16], uint8_t output[16],
                              uint32_t *written)
{
    uint8_t tag[16];
    size_t n = 0;
    *written = 0;
    int result = mbedtls_gcm_finish(gcm_of(ctx), output, 16, &n, tag, sizeof tag);
    mbedtls_gcm_free(gcm_of(ctx));
    mbedtls_platform_zeroize(ctx, sizeof *ctx);
    if (result != 0 || n > 16) {
        mbedtls_platform_zeroize(output, 16);
        return SUF2_E_CRYPTO_BACKEND;
    }
    uint8_t different = 0;
    for (unsigned i = 0; i < sizeof tag; i++) different |= tag[i] ^ expected[i];
    if (different) {
        mbedtls_platform_zeroize(output, 16);
        return SUF2_E_GCM_TAG;
    }
    *written = (uint32_t)n;
    return SUF2_OK;
}

/*
 * Minimal Mbed TLS configuration for SUF2 verification: SHA-256 and
 * ECDSA P-256 verification and AES-256-GCM decryption. No RNG, TLS, PSA or signing code.
 */
#ifndef SUF2_MBEDTLS_CONFIG_H
#define SUF2_MBEDTLS_CONFIG_H

#define MBEDTLS_HAVE_ASM

#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA256_SMALLER
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C
#define MBEDTLS_GCM_C

#define MBEDTLS_BIGNUM_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_NIST_OPTIM

/* Favour RAM and code size over speed on small cores. */
#define MBEDTLS_ECP_WINDOW_SIZE 2
#define MBEDTLS_ECP_FIXED_POINT_OPTIM 0
#define MBEDTLS_MPI_MAX_SIZE 64

#endif

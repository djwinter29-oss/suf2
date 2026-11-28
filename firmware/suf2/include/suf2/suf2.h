/*
 * SUF2 receiver and verifier core.
 *
 * Portable C99, no heap, no platform headers. The caller owns all memory and
 * provides storage callbacks. Implements sections 3-9 of
 * docs/spec/suf2-specification.md for format version 1.
 *
 * Encrypted packages require a provisioned AES-256 key and an AES-GCM port.
 * Candidate verification authenticates ciphertext and checks the decrypted
 * digest without exposing plaintext.
 */
#ifndef SUF2_SUF2_H
#define SUF2_SUF2_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUF2_BLOCK_SIZE 512u
#define SUF2_MANIFEST_SIZE 256u
#define SUF2_CHUNK_SIZE 256u
#define SUF2_ID_SIZE 16u
#define SUF2_DIGEST_SIZE 32u
#define SUF2_PUBLIC_KEY_SIZE 65u /* 0x04 || X || Y */
#define SUF2_SIGNATURE_SIZE 64u  /* r || s */
#define SUF2_AES_KEY_SIZE 32u

#define SUF2_UF2_MAGIC_START0 0x0A324655u
#define SUF2_UF2_MAGIC_START1 0x9E5D5157u
#define SUF2_UF2_MAGIC_END 0x0AB16F30u
#define SUF2_UF2_FLAG_FAMILY_ID 0x00002000u
#define SUF2_BLOCK_FLAGS 0x00002001u
#define SUF2_FAMILY_ID 0x1E0424EDu
#define SUF2_PAYLOAD_SIZE 272u
#define SUF2_FORMAT_VERSION 1u
#define SUF2_MANIFEST_FLAG_ENCRYPTED 0x00000001u

/* Manifest field offsets (section 6). */
#define SUF2_M_MAGIC 0u
#define SUF2_M_VERSION 8u
#define SUF2_M_FLAGS 12u
#define SUF2_M_PRODUCT_ID 16u
#define SUF2_M_LOAD_ADDRESS 32u
#define SUF2_M_IMAGE_LENGTH 36u
#define SUF2_M_SIGNING_KEY_ID 40u
#define SUF2_M_ENCRYPTION_KEY_ID 56u
#define SUF2_M_NONCE 72u
#define SUF2_M_RESERVED1 84u
#define SUF2_M_PLAINTEXT_DIGEST 96u
#define SUF2_M_STORED_DIGEST 128u
#define SUF2_M_TAG 160u
#define SUF2_M_RESERVED2 176u
#define SUF2_M_SIGNATURE 192u

/*
 * Status codes, grouped so a host can tell malformed input, policy rejection,
 * cryptographic failure and storage failure apart.
 */
typedef enum {
    SUF2_OK = 0,

    /* Block and session (sections 3-5, 8) */
    SUF2_E_BLOCK_FORMAT = 10,     /* flags, targetAddr, payloadSize or zero tail */
    SUF2_E_BLOCK_BOUNDS,          /* numBlocks or blockNo outside local limits */
    SUF2_E_PACKAGE_MISMATCH,      /* different package identifier or numBlocks */
    SUF2_E_CONFLICTING_DUPLICATE, /* same blockNo, different content */
    SUF2_E_BLOCK_COUNT,           /* numBlocks does not match the signed length */
    SUF2_E_PADDING,               /* nonzero padding in the last chunk */
    SUF2_E_SESSION_FAILED,        /* block dropped: session already failed */
    SUF2_E_SESSION_COMPLETE,      /* block dropped: candidate already complete */

    /* Manifest (section 6) */
    SUF2_E_MANIFEST_FORMAT = 30,  /* magic, flags, length, reserved or zero fields */
    SUF2_E_VERSION,               /* unsupported format version */
    SUF2_E_PACKAGE_ID,            /* identifier does not match the manifest */

    /* Local policy (section 9.1 step 2) */
    SUF2_E_POLICY_PRODUCT = 50,
    SUF2_E_POLICY_ADDRESS,
    SUF2_E_POLICY_SIZE,
    SUF2_E_POLICY_ENCRYPTION_REQUIRED,

    /* Cryptography (section 9.1 steps 3-5, 9.2) */
    SUF2_E_UNKNOWN_KEY = 70,      /* no provisioned key with this identifier */
    SUF2_E_SIGNATURE,             /* range check or ECDSA verification failed */
    SUF2_E_STORED_DIGEST,
    SUF2_E_NO_DECRYPTION_KEY,
    SUF2_E_PLAINTEXT_DIGEST,
    SUF2_E_CRYPTO_BACKEND,        /* library failure or invalid provisioned key */
    SUF2_E_GCM_TAG,

    /* Storage and API use */
    SUF2_E_STORAGE = 90,
    SUF2_E_ARGUMENT,
} suf2_status_t;

const char *suf2_status_name(suf2_status_t status);

/* ---- Manifest ---------------------------------------------------------- */

typedef struct {
    uint32_t version;
    uint32_t flags;
    uint32_t load_address;
    uint32_t image_length;
} suf2_manifest_t;

/* Section 6 structural rules. Fills `out` (may be NULL) on success. */
suf2_status_t suf2_manifest_parse(const uint8_t manifest[SUF2_MANIFEST_SIZE], suf2_manifest_t *out);

/* First 16 bytes of SHA-256 over the 256-byte manifest (section 4). */
suf2_status_t suf2_package_id(const uint8_t manifest[SUF2_MANIFEST_SIZE], uint8_t id[SUF2_ID_SIZE]);

/* ---- Storage ----------------------------------------------------------- */

/*
 * Staging storage for one candidate's stored image, provided by the platform.
 * write_chunk stores 256 bytes at offset 256 * index and must not return
 * SUF2_OK unless the bytes were stored. read returns previously stored bytes.
 */
typedef struct {
    void *ctx;
    suf2_status_t (*write_chunk)(void *ctx, uint32_t index, const uint8_t data[SUF2_CHUNK_SIZE]);
    suf2_status_t (*read)(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len);
} suf2_staging_t;

/* Read-only access to an image (staged or installed). */
typedef struct {
    void *ctx;
    suf2_status_t (*read)(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len);
} suf2_reader_t;

/* ---- Policy ------------------------------------------------------------ */

typedef struct {
    uint8_t public_key[SUF2_PUBLIC_KEY_SIZE]; /* uncompressed P-256 point */
} suf2_signing_key_t;

typedef struct {
    uint8_t key_id[SUF2_ID_SIZE]; /* opaque provisioned label, not a key hash */
    uint8_t key[SUF2_AES_KEY_SIZE];
} suf2_decryption_key_t;

typedef struct {
    uint8_t product_id[SUF2_ID_SIZE];
    uint32_t execution_base;    /* the only load address this device accepts */
    uint32_t max_image_size;    /* bytes */
    bool require_encryption;
    const suf2_signing_key_t *keys;
    size_t key_count;
    const suf2_decryption_key_t *decryption_keys;
    size_t decryption_key_count;
} suf2_policy_t;

/* Select a provisioned AES key and construct the exact SUF2 AAD. The context
 * is defined in suf2_crypto.h. Verify staging before installing any plaintext;
 * keep ciphertext immutable and prevent boot until installation is committed.
 */
struct suf2_gcm;
suf2_status_t suf2_decrypt_start(struct suf2_gcm *ctx,
                                 const uint8_t manifest[SUF2_MANIFEST_SIZE],
                                 const suf2_policy_t *policy);

/* ---- Receiver session (section 8) -------------------------------------- */

typedef enum {
    SUF2_STATE_IDLE = 0,
    SUF2_STATE_COLLECTING,
    SUF2_STATE_COMPLETE,
    SUF2_STATE_FAILED,
} suf2_state_t;

typedef enum {
    SUF2_SECTOR_IGNORED = 0,  /* not a SUF2 block; never affects the session */
    SUF2_SECTOR_STAGED,       /* new block stored */
    SUF2_SECTOR_DUPLICATE,    /* identical repeat, accepted without change */
    SUF2_SECTOR_REJECTED,     /* caused a failure, or was reported and dropped */
} suf2_sector_result_t;

typedef struct {
    const suf2_staging_t *staging;
    uint32_t staging_capacity;  /* bytes */
    uint8_t *bitmap;            /* caller-owned, one bit per block */
    uint32_t bitmap_size;       /* bytes */
} suf2_session_config_t;

typedef struct {
    suf2_session_config_t cfg;
    uint32_t max_blocks;
    suf2_state_t state;
    suf2_status_t error;        /* cause of the failure that ended the session */
    uint8_t package_id[SUF2_ID_SIZE];
    uint32_t num_blocks;
    uint32_t received;
    bool have_manifest;
    uint32_t image_length;      /* valid once have_manifest */
    uint8_t manifest[SUF2_MANIFEST_SIZE];
} suf2_session_t;

suf2_status_t suf2_session_init(suf2_session_t *s, const suf2_session_config_t *cfg);

/* Local reset: discards any session (section 8.2). */
void suf2_session_reset(suf2_session_t *s);

/*
 * Offer one 512-byte sector. `status` (may be NULL) receives the reported
 * cause for rejected sectors. Once s->state is SUF2_STATE_COMPLETE the
 * candidate is frozen: call suf2_verify_candidate.
 */
suf2_sector_result_t suf2_session_write(suf2_session_t *s, const uint8_t sector[SUF2_BLOCK_SIZE],
                                        suf2_status_t *status);

/* Reader over the session's staged stored image. */
suf2_reader_t suf2_session_staged_image(const suf2_session_t *s);

/* ---- Verification (section 9) ------------------------------------------ */

/*
 * Section 9.1 on a complete session, in the mandated order. On failure the
 * session moves to SUF2_STATE_FAILED with the cause in s->error.
 */
suf2_status_t suf2_verify_candidate(suf2_session_t *s, const suf2_policy_t *policy);

/*
 * Section 9.1 on a manifest and staged stored image held elsewhere, such as
 * an install journal. The manifest is untrusted and fully re-checked.
 */
suf2_status_t suf2_verify_staged(const uint8_t manifest[SUF2_MANIFEST_SIZE], const suf2_policy_t *policy,
                                 const suf2_reader_t *stored_image);

/*
 * Section 9.2: retained manifest plus installed plaintext image. Hashes
 * exactly the first L bytes read through `image`.
 */
suf2_status_t suf2_verify_boot(const uint8_t manifest[SUF2_MANIFEST_SIZE], const suf2_policy_t *policy,
                               const suf2_reader_t *image);

/* SHA-256 over the first `length` bytes read through `reader`. */
suf2_status_t suf2_hash_image(const suf2_reader_t *reader, uint32_t length, uint8_t digest[SUF2_DIGEST_SIZE]);

/* Signing-key identifier for a 65-byte uncompressed public key (section 6.1). */
suf2_status_t suf2_key_id(const uint8_t public_key[SUF2_PUBLIC_KEY_SIZE], uint8_t id[SUF2_ID_SIZE]);

#ifdef __cplusplus
}
#endif

#endif

/* SUF2 core: block classification, sessions and verification. */
#include "suf2/suf2.h"

#include <string.h>

#include "suf2/suf2_crypto.h"

static const uint8_t k_manifest_magic[8] = {'S', 'U', 'F', '2', 'M', 'F', 'S', 'T'};
static const char k_sign_prefix[] = "SUF2-MANIFEST-V1"; /* followed by 0x00 */

/* P-256 group order minus one, and (n - 1) / 2, big-endian. */
static const uint8_t k_n_minus_1[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xBC, 0xE6, 0xFA, 0xAD, 0xA7, 0x17, 0x9E, 0x84, 0xF3, 0xB9, 0xCA, 0xC2, 0xFC, 0x63, 0x25, 0x50};
static const uint8_t k_half_n[32] = {
    0x7F, 0xFF, 0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xDE, 0x73, 0x7D, 0x56, 0xD3, 0x8B, 0xCF, 0x42, 0x79, 0xDC, 0xE5, 0x61, 0x7E, 0x31, 0x92, 0xA8};

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool all_zero(const uint8_t *p, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= p[i];
    return acc == 0;
}

static bool equal_ct(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t acc = 0;
    for (size_t i = 0; i < n; i++) acc |= (uint8_t)(a[i] ^ b[i]);
    return acc == 0;
}

/* Big-endian compare of 32-byte integers: <0, 0, >0. */
static int cmp32(const uint8_t *a, const uint8_t *b)
{
    for (size_t i = 0; i < 32; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

static uint32_t chunk_count(uint32_t length)
{
    return length / SUF2_CHUNK_SIZE + (length % SUF2_CHUNK_SIZE != 0);
}

const char *suf2_status_name(suf2_status_t status)
{
    switch (status) {
    case SUF2_OK: return "OK";
    case SUF2_E_BLOCK_FORMAT: return "BLOCK_FORMAT";
    case SUF2_E_BLOCK_BOUNDS: return "BLOCK_BOUNDS";
    case SUF2_E_PACKAGE_MISMATCH: return "PACKAGE_MISMATCH";
    case SUF2_E_CONFLICTING_DUPLICATE: return "CONFLICTING_DUPLICATE";
    case SUF2_E_BLOCK_COUNT: return "BLOCK_COUNT";
    case SUF2_E_PADDING: return "PADDING";
    case SUF2_E_SESSION_FAILED: return "SESSION_FAILED";
    case SUF2_E_SESSION_COMPLETE: return "SESSION_COMPLETE";
    case SUF2_E_MANIFEST_FORMAT: return "MANIFEST_FORMAT";
    case SUF2_E_VERSION: return "VERSION";
    case SUF2_E_PACKAGE_ID: return "PACKAGE_ID";
    case SUF2_E_POLICY_PRODUCT: return "POLICY_PRODUCT";
    case SUF2_E_POLICY_ADDRESS: return "POLICY_ADDRESS";
    case SUF2_E_POLICY_SIZE: return "POLICY_SIZE";
    case SUF2_E_POLICY_ENCRYPTION_REQUIRED: return "POLICY_ENCRYPTION_REQUIRED";
    case SUF2_E_UNKNOWN_KEY: return "UNKNOWN_KEY";
    case SUF2_E_SIGNATURE: return "SIGNATURE";
    case SUF2_E_STORED_DIGEST: return "STORED_DIGEST";
    case SUF2_E_NO_DECRYPTION_KEY: return "NO_DECRYPTION_KEY";
    case SUF2_E_PLAINTEXT_DIGEST: return "PLAINTEXT_DIGEST";
    case SUF2_E_CRYPTO_BACKEND: return "CRYPTO_BACKEND";
    case SUF2_E_STORAGE: return "STORAGE";
    case SUF2_E_ARGUMENT: return "ARGUMENT";
    }
    return "UNKNOWN";
}

/* ---- Hashing ----------------------------------------------------------- */

static suf2_status_t sha256(const uint8_t *a, uint32_t alen, const uint8_t *b, uint32_t blen,
                            uint8_t digest[SUF2_DIGEST_SIZE])
{
    suf2_sha256_t ctx;
    suf2_status_t st = suf2_sha256_start(&ctx);
    if (st != SUF2_OK) return st;
    st = suf2_sha256_update(&ctx, a, alen);
    if (st == SUF2_OK && blen) st = suf2_sha256_update(&ctx, b, blen);
    suf2_status_t fin = suf2_sha256_finish(&ctx, digest);
    return st != SUF2_OK ? st : fin;
}

suf2_status_t suf2_package_id(const uint8_t manifest[SUF2_MANIFEST_SIZE], uint8_t id[SUF2_ID_SIZE])
{
    uint8_t digest[SUF2_DIGEST_SIZE];
    if (!manifest || !id) return SUF2_E_ARGUMENT;
    suf2_status_t st = sha256(manifest, SUF2_MANIFEST_SIZE, NULL, 0, digest);
    if (st == SUF2_OK) memcpy(id, digest, SUF2_ID_SIZE);
    return st;
}

suf2_status_t suf2_key_id(const uint8_t public_key[SUF2_PUBLIC_KEY_SIZE], uint8_t id[SUF2_ID_SIZE])
{
    uint8_t digest[SUF2_DIGEST_SIZE];
    if (!public_key || !id) return SUF2_E_ARGUMENT;
    suf2_status_t st = sha256(public_key, SUF2_PUBLIC_KEY_SIZE, NULL, 0, digest);
    if (st == SUF2_OK) memcpy(id, digest, SUF2_ID_SIZE);
    return st;
}

suf2_status_t suf2_hash_image(const suf2_reader_t *reader, uint32_t length, uint8_t digest[SUF2_DIGEST_SIZE])
{
    uint8_t buf[SUF2_CHUNK_SIZE];
    suf2_sha256_t ctx;
    if (!reader || !reader->read || !digest) return SUF2_E_ARGUMENT;
    suf2_status_t st = suf2_sha256_start(&ctx);
    if (st != SUF2_OK) return st;
    for (uint32_t off = 0; off < length && st == SUF2_OK;) {
        uint32_t n = length - off < sizeof buf ? length - off : (uint32_t)sizeof buf;
        st = reader->read(reader->ctx, off, buf, n);
        if (st != SUF2_OK) st = SUF2_E_STORAGE;
        else st = suf2_sha256_update(&ctx, buf, n);
        off += n;
    }
    suf2_status_t fin = suf2_sha256_finish(&ctx, digest);
    return st != SUF2_OK ? st : fin;
}

/* ---- Manifest ---------------------------------------------------------- */

suf2_status_t suf2_manifest_parse(const uint8_t m[SUF2_MANIFEST_SIZE], suf2_manifest_t *out)
{
    if (!m) return SUF2_E_ARGUMENT;
    if (memcmp(m + SUF2_M_MAGIC, k_manifest_magic, sizeof k_manifest_magic) != 0) return SUF2_E_MANIFEST_FORMAT;
    uint32_t version = rd32(m + SUF2_M_VERSION);
    if (version != SUF2_FORMAT_VERSION) return SUF2_E_VERSION;
    uint32_t flags = rd32(m + SUF2_M_FLAGS);
    uint32_t length = rd32(m + SUF2_M_IMAGE_LENGTH);
    if (flags & ~SUF2_MANIFEST_FLAG_ENCRYPTED) return SUF2_E_MANIFEST_FORMAT;
    if (length == 0) return SUF2_E_MANIFEST_FORMAT;
    if (!all_zero(m + SUF2_M_RESERVED1, 12) || !all_zero(m + SUF2_M_RESERVED2, 16)) return SUF2_E_MANIFEST_FORMAT;
    if (flags & SUF2_MANIFEST_FLAG_ENCRYPTED) {
        if (all_zero(m + SUF2_M_ENCRYPTION_KEY_ID, SUF2_ID_SIZE)) return SUF2_E_MANIFEST_FORMAT;
    } else {
        if (!all_zero(m + SUF2_M_ENCRYPTION_KEY_ID, SUF2_ID_SIZE) || !all_zero(m + SUF2_M_NONCE, 12) ||
            !all_zero(m + SUF2_M_TAG, 16))
            return SUF2_E_MANIFEST_FORMAT;
        if (!equal_ct(m + SUF2_M_PLAINTEXT_DIGEST, m + SUF2_M_STORED_DIGEST, SUF2_DIGEST_SIZE))
            return SUF2_E_MANIFEST_FORMAT;
    }
    if (out) {
        out->version = version;
        out->flags = flags;
        out->load_address = rd32(m + SUF2_M_LOAD_ADDRESS);
        out->image_length = length;
    }
    return SUF2_OK;
}

/* ---- Session ----------------------------------------------------------- */

static bool bit_get(const suf2_session_t *s, uint32_t i)
{
    return (s->cfg.bitmap[i >> 3] >> (i & 7)) & 1u;
}

static void bit_set(suf2_session_t *s, uint32_t i)
{
    s->cfg.bitmap[i >> 3] = (uint8_t)(s->cfg.bitmap[i >> 3] | (1u << (i & 7)));
}

suf2_status_t suf2_session_init(suf2_session_t *s, const suf2_session_config_t *cfg)
{
    if (!s || !cfg || !cfg->staging || !cfg->staging->write_chunk || !cfg->staging->read || !cfg->bitmap ||
        cfg->bitmap_size == 0 || cfg->staging_capacity < SUF2_CHUNK_SIZE)
        return SUF2_E_ARGUMENT;
    memset(s, 0, sizeof *s);
    s->cfg = *cfg;
    uint32_t by_capacity = cfg->staging_capacity / SUF2_CHUNK_SIZE + 1u;
    uint32_t by_bitmap = cfg->bitmap_size > UINT32_MAX / 8u ? UINT32_MAX : cfg->bitmap_size * 8u;
    s->max_blocks = by_capacity < by_bitmap ? by_capacity : by_bitmap;
    suf2_session_reset(s);
    return SUF2_OK;
}

void suf2_session_reset(suf2_session_t *s)
{
    suf2_session_config_t cfg = s->cfg;
    uint32_t max_blocks = s->max_blocks;
    memset(s, 0, sizeof *s);
    s->cfg = cfg;
    s->max_blocks = max_blocks;
    s->state = SUF2_STATE_IDLE;
}

static suf2_sector_result_t fail(suf2_session_t *s, suf2_status_t cause, suf2_status_t *status)
{
    s->state = SUF2_STATE_FAILED;
    s->error = cause;
    if (status) *status = cause;
    return SUF2_SECTOR_REJECTED;
}

static uint32_t last_chunk_useful(uint32_t length)
{
    uint32_t r = length % SUF2_CHUNK_SIZE;
    return r ? r : SUF2_CHUNK_SIZE;
}

/* Checks that need no manifest (sections 3 and 8.2). */
static suf2_status_t check_block(const suf2_session_t *s, const uint8_t *b)
{
    if (rd32(b + 8) != SUF2_BLOCK_FLAGS || rd32(b + 12) != 0 || rd32(b + 16) != SUF2_PAYLOAD_SIZE)
        return SUF2_E_BLOCK_FORMAT;
    if (!all_zero(b + 32 + SUF2_PAYLOAD_SIZE, 476u - SUF2_PAYLOAD_SIZE)) return SUF2_E_BLOCK_FORMAT;
    uint32_t block_no = rd32(b + 20), num_blocks = rd32(b + 24);
    if (num_blocks < 2 || num_blocks > s->max_blocks || block_no >= num_blocks) return SUF2_E_BLOCK_BOUNDS;
    return SUF2_OK;
}

static suf2_status_t staged_equals(const suf2_session_t *s, uint32_t block_no, const uint8_t *content, bool *equal)
{
    uint8_t buf[SUF2_CHUNK_SIZE];
    if (block_no == 0) {
        *equal = s->have_manifest && memcmp(s->manifest, content, SUF2_MANIFEST_SIZE) == 0;
        return SUF2_OK;
    }
    if (s->cfg.staging->read(s->cfg.staging->ctx, (block_no - 1u) * SUF2_CHUNK_SIZE, buf, sizeof buf) != SUF2_OK)
        return SUF2_E_STORAGE;
    *equal = memcmp(buf, content, SUF2_CHUNK_SIZE) == 0;
    return SUF2_OK;
}

static suf2_status_t accept_manifest(suf2_session_t *s, const uint8_t *m)
{
    suf2_manifest_t info;
    uint8_t id[SUF2_ID_SIZE];
    suf2_status_t st = suf2_manifest_parse(m, &info);
    if (st != SUF2_OK) return st;
    st = suf2_package_id(m, id);
    if (st != SUF2_OK) return st;
    if (memcmp(id, s->package_id, SUF2_ID_SIZE) != 0) return SUF2_E_PACKAGE_ID;
    uint32_t chunks = chunk_count(info.image_length);
    if (chunks + 1u != s->num_blocks) return SUF2_E_BLOCK_COUNT;
    if (bit_get(s, s->num_blocks - 1u)) {
        uint8_t buf[SUF2_CHUNK_SIZE];
        if (s->cfg.staging->read(s->cfg.staging->ctx, (chunks - 1u) * SUF2_CHUNK_SIZE, buf, sizeof buf) != SUF2_OK)
            return SUF2_E_STORAGE;
        uint32_t useful = last_chunk_useful(info.image_length);
        if (!all_zero(buf + useful, SUF2_CHUNK_SIZE - useful)) return SUF2_E_PADDING;
    }
    memcpy(s->manifest, m, SUF2_MANIFEST_SIZE);
    s->image_length = info.image_length;
    s->have_manifest = true;
    return SUF2_OK;
}

suf2_sector_result_t suf2_session_write(suf2_session_t *s, const uint8_t b[SUF2_BLOCK_SIZE], suf2_status_t *status)
{
    if (status) *status = SUF2_OK;
    if (!s || !b) {
        if (status) *status = SUF2_E_ARGUMENT;
        return SUF2_SECTOR_REJECTED;
    }
    /* Section 8.1 classification. */
    if (rd32(b) != SUF2_UF2_MAGIC_START0 || rd32(b + 4) != SUF2_UF2_MAGIC_START1 ||
        rd32(b + 508) != SUF2_UF2_MAGIC_END)
        return SUF2_SECTOR_IGNORED;
    if (!(rd32(b + 8) & SUF2_UF2_FLAG_FAMILY_ID) || rd32(b + 28) != SUF2_FAMILY_ID) return SUF2_SECTOR_IGNORED;

    if (s->state == SUF2_STATE_FAILED) {
        if (status) *status = SUF2_E_SESSION_FAILED;
        return SUF2_SECTOR_REJECTED;
    }

    suf2_status_t st = check_block(s, b);
    uint32_t block_no = rd32(b + 20), num_blocks = rd32(b + 24);
    const uint8_t *id = b + 32, *content = b + 48;

    if (s->state == SUF2_STATE_COMPLETE) {
        bool equal = false;
        if (st == SUF2_OK && num_blocks == s->num_blocks && memcmp(id, s->package_id, SUF2_ID_SIZE) == 0 &&
            staged_equals(s, block_no, content, &equal) == SUF2_OK && equal)
            return SUF2_SECTOR_DUPLICATE;
        if (status) *status = SUF2_E_SESSION_COMPLETE;
        return SUF2_SECTOR_REJECTED;
    }

    if (st != SUF2_OK) return fail(s, st, status);

    if (s->state == SUF2_STATE_IDLE) {
        memcpy(s->package_id, id, SUF2_ID_SIZE);
        s->num_blocks = num_blocks;
        s->received = 0;
        memset(s->cfg.bitmap, 0, (num_blocks + 7u) / 8u);
        s->state = SUF2_STATE_COLLECTING;
    } else if (num_blocks != s->num_blocks || memcmp(id, s->package_id, SUF2_ID_SIZE) != 0) {
        return fail(s, SUF2_E_PACKAGE_MISMATCH, status);
    }

    if (bit_get(s, block_no)) {
        bool equal = false;
        st = staged_equals(s, block_no, content, &equal);
        if (st != SUF2_OK) return fail(s, st, status);
        if (!equal) return fail(s, SUF2_E_CONFLICTING_DUPLICATE, status);
        return SUF2_SECTOR_DUPLICATE;
    }

    if (block_no == 0) {
        st = accept_manifest(s, content);
    } else {
        if (s->have_manifest && block_no == s->num_blocks - 1u) {
            uint32_t useful = last_chunk_useful(s->image_length);
            if (!all_zero(content + useful, SUF2_CHUNK_SIZE - useful)) st = SUF2_E_PADDING;
        }
        if (st == SUF2_OK &&
            s->cfg.staging->write_chunk(s->cfg.staging->ctx, block_no - 1u, content) != SUF2_OK)
            st = SUF2_E_STORAGE;
    }
    if (st != SUF2_OK) return fail(s, st, status);

    bit_set(s, block_no);
    s->received++;
    if (s->received == s->num_blocks) s->state = SUF2_STATE_COMPLETE;
    return SUF2_SECTOR_STAGED;
}

static suf2_status_t staged_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const suf2_session_t *s = (const suf2_session_t *)ctx;
    return s->cfg.staging->read(s->cfg.staging->ctx, offset, buf, len);
}

suf2_reader_t suf2_session_staged_image(const suf2_session_t *s)
{
    suf2_reader_t r = {(void *)s, staged_read};
    return r;
}

/* ---- Verification ------------------------------------------------------ */

static suf2_status_t check_policy(const suf2_manifest_t *info, const uint8_t *m, const suf2_policy_t *p,
                                  bool at_acceptance)
{
    if (memcmp(m + SUF2_M_PRODUCT_ID, p->product_id, SUF2_ID_SIZE) != 0) return SUF2_E_POLICY_PRODUCT;
    if (info->load_address != p->execution_base) return SUF2_E_POLICY_ADDRESS;
    if (info->image_length > p->max_image_size ||
        (uint64_t)info->load_address + info->image_length > 0x100000000ull)
        return SUF2_E_POLICY_SIZE;
    if (at_acceptance && p->require_encryption && !(info->flags & SUF2_MANIFEST_FLAG_ENCRYPTED))
        return SUF2_E_POLICY_ENCRYPTION_REQUIRED;
    return SUF2_OK;
}

static suf2_status_t check_signature(const uint8_t *m, const suf2_policy_t *p)
{
    const uint8_t *key = NULL;
    const uint8_t *sig = m + SUF2_M_SIGNATURE;
    uint8_t id[SUF2_ID_SIZE], hash[SUF2_DIGEST_SIZE];
    for (size_t i = 0; i < p->key_count; i++) {
        suf2_status_t st = suf2_key_id(p->keys[i].public_key, id);
        if (st != SUF2_OK) return st;
        if (memcmp(id, m + SUF2_M_SIGNING_KEY_ID, SUF2_ID_SIZE) == 0) {
            key = p->keys[i].public_key;
            break;
        }
    }
    if (!key) return SUF2_E_UNKNOWN_KEY;
    /* 1 <= r <= n-1 and 1 <= s <= (n-1)/2 (section 7). */
    if (all_zero(sig, 32) || cmp32(sig, k_n_minus_1) > 0) return SUF2_E_SIGNATURE;
    if (all_zero(sig + 32, 32) || cmp32(sig + 32, k_half_n) > 0) return SUF2_E_SIGNATURE;
    /* Hash the signed message once; the backend takes the hash. */
    suf2_status_t st = sha256((const uint8_t *)k_sign_prefix, sizeof k_sign_prefix, m, SUF2_M_SIGNATURE, hash);
    if (st != SUF2_OK) return st;
    return suf2_p256_verify(key, hash, sig);
}

static suf2_status_t check_digest(const suf2_reader_t *r, uint32_t length, const uint8_t *expected,
                                  suf2_status_t mismatch)
{
    uint8_t digest[SUF2_DIGEST_SIZE];
    suf2_status_t st = suf2_hash_image(r, length, digest);
    if (st != SUF2_OK) return st;
    return equal_ct(digest, expected, SUF2_DIGEST_SIZE) ? SUF2_OK : mismatch;
}

suf2_status_t suf2_verify_staged(const uint8_t m[SUF2_MANIFEST_SIZE], const suf2_policy_t *p,
                                 const suf2_reader_t *stored)
{
    suf2_manifest_t info;
    if (!m || !p || !stored) return SUF2_E_ARGUMENT;
    suf2_status_t st = suf2_manifest_parse(m, &info);
    if (st == SUF2_OK) st = check_policy(&info, m, p, true);
    if (st == SUF2_OK) st = check_signature(m, p);
    if (st == SUF2_OK) st = check_digest(stored, info.image_length, m + SUF2_M_STORED_DIGEST, SUF2_E_STORED_DIGEST);
    /* No AES-GCM decryptor is provisioned in this core (section 9.1 step 5). */
    if (st == SUF2_OK && (info.flags & SUF2_MANIFEST_FLAG_ENCRYPTED)) st = SUF2_E_NO_DECRYPTION_KEY;
    return st;
}

static suf2_status_t verify_candidate(const suf2_session_t *s, const suf2_policy_t *p)
{
    uint8_t id[SUF2_ID_SIZE];
    suf2_manifest_t info;
    /* Step 1: structure, re-derived from the frozen candidate. */
    suf2_status_t st = suf2_manifest_parse(s->manifest, &info);
    if (st != SUF2_OK) return st;
    st = suf2_package_id(s->manifest, id);
    if (st != SUF2_OK) return st;
    if (memcmp(id, s->package_id, SUF2_ID_SIZE) != 0) return SUF2_E_PACKAGE_ID;
    if (chunk_count(info.image_length) + 1u != s->num_blocks) return SUF2_E_BLOCK_COUNT;
    suf2_reader_t staged = suf2_session_staged_image(s);
    return suf2_verify_staged(s->manifest, p, &staged);
}

suf2_status_t suf2_verify_candidate(suf2_session_t *s, const suf2_policy_t *p)
{
    if (!s || !p || s->state != SUF2_STATE_COMPLETE || !s->have_manifest) return SUF2_E_ARGUMENT;
    suf2_status_t st = verify_candidate(s, p);
    if (st != SUF2_OK) {
        s->state = SUF2_STATE_FAILED;
        s->error = st;
    }
    return st;
}

suf2_status_t suf2_verify_boot(const uint8_t m[SUF2_MANIFEST_SIZE], const suf2_policy_t *p, const suf2_reader_t *image)
{
    suf2_manifest_t info;
    if (!m || !p || !image) return SUF2_E_ARGUMENT;
    suf2_status_t st = suf2_manifest_parse(m, &info);
    if (st == SUF2_OK) st = check_policy(&info, m, p, false);
    if (st == SUF2_OK) st = check_signature(m, p);
    if (st == SUF2_OK)
        st = check_digest(image, info.image_length, m + SUF2_M_PLAINTEXT_DIGEST, SUF2_E_PLAINTEXT_DIGEST);
    return st;
}

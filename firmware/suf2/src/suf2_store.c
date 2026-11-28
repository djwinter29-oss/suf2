#include "suf2/suf2_store.h"
#include "suf2/suf2_crypto.h"

#include <string.h>

#define SECTOR SUF2_STORE_SECTOR_SIZE
#define PAGE SUF2_STORE_PAGE_SIZE
#define JOURNAL_MAGIC 0x4C4A3253u
#define COMMIT_MAGIC 0x4B4F3253u
#define JOURNAL_VERSION 1u
#define COMMIT_OFFSET (SECTOR - PAGE)
#define JOURNAL_SIZE (8u + SUF2_MANIFEST_SIZE + 4u)
#define COMMIT_SIZE (4u + SUF2_ID_SIZE + 4u)

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

suf2_status_t suf2_store_init(suf2_store_t *s, const suf2_store_config_t *c)
{
    if (!s || !c || !c->read || !c->erase || !c->program || !c->erased_bitmap ||
        !c->slot_size || c->meta_offset % SECTOR || c->app_offset % SECTOR ||
        c->staging_offset % SECTOR || c->slot_size % SECTOR ||
        c->meta_offset > c->flash_size || SECTOR > c->flash_size - c->meta_offset ||
        c->app_offset < c->meta_offset + SECTOR ||
        c->app_offset > c->staging_offset || c->slot_size > c->staging_offset - c->app_offset ||
        c->staging_offset > c->flash_size || c->slot_size > c->flash_size - c->staging_offset ||
        c->erased_bitmap_size < (c->slot_size / SECTOR + 7u) / 8u)
        return SUF2_E_ARGUMENT;
    s->cfg = *c;
    suf2_store_session_reset(s);
    return SUF2_OK;
}

void suf2_store_session_reset(suf2_store_t *s)
{
    memset(s->cfg.erased_bitmap, 0, s->cfg.erased_bitmap_size);
}

static bool writable(const suf2_store_t *s, uint32_t offset, uint32_t len)
{
    return offset >= s->cfg.meta_offset && len <= s->cfg.flash_size && offset <= s->cfg.flash_size - len;
}

static suf2_status_t erase(suf2_store_t *s, uint32_t offset, uint32_t len)
{
    if (!writable(s, offset, len) || offset % SECTOR || len % SECTOR) return SUF2_E_STORAGE;
    return s->cfg.erase(s->cfg.ctx, offset, len);
}

static suf2_status_t program(suf2_store_t *s, uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (!writable(s, offset, len) || offset % PAGE || len % PAGE) return SUF2_E_STORAGE;
    return s->cfg.program(s->cfg.ctx, offset, data, len);
}

static suf2_status_t region_read(suf2_store_t *s, uint32_t base, uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (len > s->cfg.slot_size || offset > s->cfg.slot_size - len) return SUF2_E_STORAGE;
    return s->cfg.read(s->cfg.ctx, base + offset, buf, len);
}

static suf2_status_t staging_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    suf2_store_t *s = ctx;
    return region_read(s, s->cfg.staging_offset, offset, buf, len);
}

static suf2_status_t app_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    suf2_store_t *s = ctx;
    return region_read(s, s->cfg.app_offset, offset, buf, len);
}

static suf2_status_t staging_write(void *ctx, uint32_t index, const uint8_t data[SUF2_CHUNK_SIZE])
{
    suf2_store_t *s = ctx;
    if (index >= s->cfg.slot_size / SUF2_CHUNK_SIZE) return SUF2_E_STORAGE;
    uint32_t rel = index * SUF2_CHUNK_SIZE;
    uint32_t sector = rel / SECTOR;
    uint8_t mask = (uint8_t)(1u << (sector % 8));
    if (!(s->cfg.erased_bitmap[sector / 8] & mask)) {
        suf2_status_t st = erase(s, s->cfg.staging_offset + sector * SECTOR, SECTOR);
        if (st != SUF2_OK) return st;
        s->cfg.erased_bitmap[sector / 8] |= mask;
    }
    return program(s, s->cfg.staging_offset + rel, data, SUF2_CHUNK_SIZE);
}

suf2_staging_t suf2_store_staging(suf2_store_t *s)
{
    return (suf2_staging_t){.ctx = s, .write_chunk = staging_write, .read = staging_read};
}

suf2_reader_t suf2_store_staging_reader(suf2_store_t *s)
{
    return (suf2_reader_t){.ctx = s, .read = staging_read};
}

suf2_reader_t suf2_store_app_reader(suf2_store_t *s)
{
    return (suf2_reader_t){.ctx = s, .read = app_read};
}

suf2_status_t suf2_store_read_meta(suf2_store_t *s, uint8_t manifest[SUF2_MANIFEST_SIZE],
                                  suf2_meta_state_t *state)
{
    uint8_t j[JOURNAL_SIZE], c[COMMIT_SIZE], id[SUF2_ID_SIZE];
    if (!s || !manifest || !state) return SUF2_E_ARGUMENT;
    *state = SUF2_META_EMPTY;
    suf2_status_t st = s->cfg.read(s->cfg.ctx, s->cfg.meta_offset, j, sizeof j);
    if (st != SUF2_OK) return st;
    if (get32(j) != JOURNAL_MAGIC || get32(j + 4) != JOURNAL_VERSION ||
        get32(j + 8 + SUF2_MANIFEST_SIZE) != ~JOURNAL_MAGIC) return SUF2_OK;
    memcpy(manifest, j + 8, SUF2_MANIFEST_SIZE);
    *state = SUF2_META_PENDING;
    st = s->cfg.read(s->cfg.ctx, s->cfg.meta_offset + COMMIT_OFFSET, c, sizeof c);
    if (st != SUF2_OK) return st;
    if (get32(c) != COMMIT_MAGIC || get32(c + 4 + SUF2_ID_SIZE) != ~COMMIT_MAGIC) return SUF2_OK;
    if ((st = suf2_package_id(manifest, id)) != SUF2_OK) return st;
    if (memcmp(id, c + 4, SUF2_ID_SIZE) == 0) *state = SUF2_META_COMMITTED;
    return SUF2_OK;
}

static suf2_status_t copy_image(suf2_store_t *s, const uint8_t *manifest, const suf2_policy_t *policy,
                                 const suf2_manifest_t *info)
{
    uint8_t *buf = s->scratch.bytes;
    bool encrypted = (info->flags & SUF2_MANIFEST_FLAG_ENCRYPTED) != 0;
    bool finished = false;
    suf2_gcm_t gcm;
    uint8_t input[PAGE], plain[PAGE + 16], tail[16];
    suf2_status_t st = encrypted ? suf2_decrypt_start(&gcm, manifest, policy) : SUF2_OK;
    if (st != SUF2_OK) return st;
    uint32_t span = (info->image_length + SECTOR - 1) / SECTOR * SECTOR;
    for (uint32_t off = 0; off < span && st == SUF2_OK; off += SECTOR) {
        st = staging_read(s, off, buf, SECTOR);
        if (st != SUF2_OK) break;
        if (encrypted) {
            uint32_t length = info->image_length - off < SECTOR ? info->image_length - off : SECTOR;
            uint32_t out = 0, written = 0;
            for (uint32_t in = 0; in < length && st == SUF2_OK;) {
                uint32_t n = length - in < PAGE ? length - in : PAGE;
                memcpy(input, buf + in, n);
                st = suf2_gcm_update(&gcm, input, n, plain, sizeof plain, &written);
                if (st == SUF2_OK && (written > sizeof plain || written > length - out))
                    st = SUF2_E_CRYPTO_BACKEND;
                if (st == SUF2_OK) {
                    memcpy(buf + out, plain, written);
                    out += written;
                }
                memset(plain, 0, sizeof plain);
                in += n;
            }
            if (st == SUF2_OK && off + length == info->image_length) {
                st = suf2_gcm_finish(&gcm, manifest + SUF2_M_TAG, tail, &written);
                finished = true;
                if (st == SUF2_OK && (written > sizeof tail || written > length - out))
                    st = SUF2_E_CRYPTO_BACKEND;
                if (st == SUF2_OK) {
                    memcpy(buf + out, tail, written);
                    out += written;
                }
                memset(tail, 0, sizeof tail);
            }
            if (st == SUF2_OK && out != length) st = SUF2_E_CRYPTO_BACKEND;
            if (st != SUF2_OK) break;
        }
        st = erase(s, s->cfg.app_offset + off, SECTOR);
        if (st == SUF2_OK) st = program(s, s->cfg.app_offset + off, buf, SECTOR);
    }
    if (encrypted && !finished) {
        uint32_t written;
        suf2_status_t finish = suf2_gcm_finish(&gcm, manifest + SUF2_M_TAG, tail, &written);
        if (st == SUF2_OK) st = finish;
        memset(tail, 0, sizeof tail);
    }
    if (st != SUF2_OK) memset(buf, 0, SECTOR);
    return st;
}

suf2_status_t suf2_store_install(suf2_store_t *s, const uint8_t manifest[SUF2_MANIFEST_SIZE],
                                const suf2_policy_t *policy)
{
    if (!s || !manifest || !policy) return SUF2_E_ARGUMENT;
    suf2_manifest_t info;
    suf2_reader_t staged = suf2_store_staging_reader(s);
    suf2_reader_t installed = suf2_store_app_reader(s);
    uint8_t id[SUF2_ID_SIZE];
    uint8_t *buf = s->scratch.bytes;

    /* Nothing destructive happens until the staged copy passes verification. */
    suf2_status_t st = suf2_verify_staged(manifest, policy, &staged);
    if (st != SUF2_OK) return st;
    if ((st = suf2_manifest_parse(manifest, &info)) != SUF2_OK) return st;
    if ((st = suf2_package_id(manifest, id)) != SUF2_OK) return st;
    if (info.image_length > s->cfg.slot_size) return SUF2_E_POLICY_SIZE;

    /* Journal first: the old application is no longer bootable after this. */
    memset(buf, 0xff, 2 * PAGE);
    put32(buf, JOURNAL_MAGIC);
    put32(buf + 4, JOURNAL_VERSION);
    memcpy(buf + 8, manifest, SUF2_MANIFEST_SIZE);
    put32(buf + 8 + SUF2_MANIFEST_SIZE, ~JOURNAL_MAGIC);
    if ((st = erase(s, s->cfg.meta_offset, SECTOR)) != SUF2_OK) return st;
    if ((st = program(s, s->cfg.meta_offset, buf, 2 * PAGE)) != SUF2_OK) return st;

    /* Verification already authenticated plaintext; preserve ciphertext in
     * immutable staging and decrypt again while copying. Interrupted installs
     * re-run all checks, and no application boots until the commit is written.
     */
    if ((st = copy_image(s, manifest, policy, &info)) != SUF2_OK) return st;
    if ((st = suf2_verify_boot(manifest, policy, &installed)) != SUF2_OK) return st;

    memset(buf, 0xff, PAGE);
    put32(buf, COMMIT_MAGIC);
    memcpy(buf + 4, id, SUF2_ID_SIZE);
    put32(buf + 4 + SUF2_ID_SIZE, ~COMMIT_MAGIC);
    return program(s, s->cfg.meta_offset + COMMIT_OFFSET, buf, PAGE);
}

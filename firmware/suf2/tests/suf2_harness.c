/*
 * Native harness for the SUF2 core. Feeds a stream of 512-byte sectors into a
 * session backed by simulated NOR, verifies and installs the candidate using
 * the reusable store, and runs boot verification. Prints one JSON object.
 *
 *   suf2_harness --stream FILE --product HEX32 --base ADDR --key PUBHEX65
 *                [--key ...] [--max-size N] [--capacity N] [--bitmap-bytes N]
 *                [--require-encryption] [--tamper-installed OFFSET]
 *                [--aes-key-file FILE] [--fail-install-after N] [--installed-out FILE]
 * AES test files contain the 16-byte label followed by the 32-byte key, never
 * secret command-line arguments.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suf2/suf2.h"
#include "suf2/suf2_store.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
} ram_t;

static long fail_after = -1;
static unsigned mutations;

static suf2_status_t flash_erase(void *ctx, uint32_t offset, uint32_t len)
{
    ram_t *r = ctx;
    if ((uint64_t)offset + len > r->size || offset % 4096 || len % 4096) return SUF2_E_STORAGE;
    if (fail_after >= 0 && mutations >= (unsigned long)fail_after) return SUF2_E_STORAGE;
    mutations++;
    memset(r->data + offset, 0xff, len);
    return SUF2_OK;
}

static suf2_status_t flash_program(void *ctx, uint32_t offset, const uint8_t *data, uint32_t len)
{
    ram_t *r = ctx;
    if ((uint64_t)offset + len > r->size || offset % 256 || len % 256) return SUF2_E_STORAGE;
    if (fail_after >= 0 && mutations >= (unsigned long)fail_after) return SUF2_E_STORAGE;
    mutations++;
    for (uint32_t i = 0; i < len; i++) {
        if ((r->data[offset + i] & data[i]) != data[i]) return SUF2_E_STORAGE;
        r->data[offset + i] &= data[i];
    }
    return SUF2_OK;
}

static suf2_status_t ram_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    ram_t *r = ctx;
    if ((uint64_t)offset + len > r->size) return SUF2_E_STORAGE;
    memcpy(buf, r->data + offset, len);
    return SUF2_OK;
}

static int hex_bytes(const char *hex, uint8_t *out, size_t n)
{
    if (strlen(hex) != n * 2) return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "bad arguments; see the comment at the top of suf2_harness.c\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *stream_path = NULL;
    suf2_signing_key_t keys[4];
    suf2_decryption_key_t aes_keys[4];
    suf2_policy_t policy = {0};
    uint32_t capacity = 64 * 1024, bitmap_bytes = 64;
    long tamper = -1;
    long interrupt_after = -1;
    const char *installed_out = NULL;
    policy.max_image_size = 64 * 1024;
    policy.keys = keys;
    policy.decryption_keys = aes_keys;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--require-encryption")) {
            policy.require_encryption = true;
            continue;
        }
        if (!v) usage();
        i++;
        if (!strcmp(a, "--stream")) stream_path = v;
        else if (!strcmp(a, "--product")) { if (hex_bytes(v, policy.product_id, 16)) usage(); }
        else if (!strcmp(a, "--base")) policy.execution_base = (uint32_t)strtoul(v, NULL, 0);
        else if (!strcmp(a, "--max-size")) policy.max_image_size = (uint32_t)strtoul(v, NULL, 0);
        else if (!strcmp(a, "--capacity")) capacity = (uint32_t)strtoul(v, NULL, 0);
        else if (!strcmp(a, "--bitmap-bytes")) bitmap_bytes = (uint32_t)strtoul(v, NULL, 0);
        else if (!strcmp(a, "--tamper-installed")) tamper = strtol(v, NULL, 0);
        else if (!strcmp(a, "--fail-install-after")) interrupt_after = strtol(v, NULL, 0);
        else if (!strcmp(a, "--installed-out")) installed_out = v;
        else if (!strcmp(a, "--aes-key-file")) {
            if (policy.decryption_key_count == 4) usage();
            FILE *key_file = fopen(v, "rb");
            if (!key_file) { perror(v); return 2; }
            suf2_decryption_key_t *k = &aes_keys[policy.decryption_key_count];
            int valid = fread(k->key_id, 1, sizeof k->key_id, key_file) == sizeof k->key_id &&
                fread(k->key, 1, sizeof k->key, key_file) == sizeof k->key && fgetc(key_file) == EOF;
            fclose(key_file);
            if (!valid) usage();
            policy.decryption_key_count++;
        }
        else if (!strcmp(a, "--key")) {
            if (policy.key_count == 4 || hex_bytes(v, keys[policy.key_count].public_key, 65)) usage();
            policy.key_count++;
        } else usage();
    }
    if (!stream_path) usage();

    FILE *f = fopen(stream_path, "rb");
    if (!f) { perror(stream_path); return 2; }

    if (capacity % 4096 || capacity > (UINT32_MAX - 4096) / 2) usage();
    ram_t flash = {malloc(4096 + 2 * capacity), 4096 + 2 * capacity};
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    uint32_t erased_bytes = (capacity / 4096 + 7) / 8;
    uint8_t *erased = calloc(1, erased_bytes);
    if (!flash.data || !bitmap || !erased) { fprintf(stderr, "allocation failed\n"); return 2; }
    memset(flash.data, 0xff, flash.size);
    suf2_store_t store;
    const suf2_store_config_t store_cfg = {
        .ctx = &flash, .flash_size = flash.size, .meta_offset = 0,
        .app_offset = 4096, .staging_offset = 4096 + capacity, .slot_size = capacity,
        .read = ram_read, .erase = flash_erase, .program = flash_program,
        .erased_bitmap = erased, .erased_bitmap_size = erased_bytes,
    };
    if (suf2_store_init(&store, &store_cfg) != SUF2_OK) return 2;
    suf2_staging_t port = suf2_store_staging(&store);
    suf2_session_config_t cfg = {&port, capacity, bitmap, bitmap_bytes};
    suf2_session_t session;
    if (suf2_session_init(&session, &cfg) != SUF2_OK) return 2;

    /* r: per-sector results (I ignored, S staged, D duplicate, R rejected). */
    char results[4096] = "";
    char reports[8192] = "";
    size_t nres = 0;
    uint8_t sector[SUF2_BLOCK_SIZE];
    while (fread(sector, 1, sizeof sector, f) == sizeof sector) {
        suf2_status_t st;
        suf2_sector_result_t r = suf2_session_write(&session, sector, &st);
        if (nres + 1 < sizeof results) results[nres++] = "ISDR"[r];
        if (r == SUF2_SECTOR_REJECTED && strlen(reports) + 40 < sizeof reports) {
            if (reports[0]) strcat(reports, ",");
            strcat(reports, "\"");
            strcat(reports, suf2_status_name(st));
            strcat(reports, "\"");
        }
    }
    fclose(f);

    static const char *states[] = {"IDLE", "COLLECTING", "COMPLETE", "FAILED"};
    const char *verify = "-", *boot = "-", *install = "-", *retry = "-";
    static const char *meta_names[] = {"EMPTY", "PENDING", "COMMITTED"};
    const char *meta_name = "-";
    bool unchanged = true;
    unsigned install_mutations = 0;
    if (session.state == SUF2_STATE_COMPLETE) {
        uint8_t *snapshot = malloc(capacity);
        if (!snapshot) { fprintf(stderr, "allocation failed\n"); return 2; }
        memcpy(snapshot, flash.data + store_cfg.staging_offset, capacity);
        mutations = 0;
        fail_after = interrupt_after;
        suf2_status_t st = suf2_verify_candidate(&session, &policy);
        verify = suf2_status_name(st);
        st = suf2_store_install(&store, session.manifest, &policy);
        install = suf2_status_name(st);
        install_mutations = mutations;
        uint8_t manifest[SUF2_MANIFEST_SIZE];
        suf2_meta_state_t meta;
        suf2_status_t read_meta = suf2_store_read_meta(&store, manifest, &meta);
        meta_name = read_meta == SUF2_OK ? meta_names[meta] : suf2_status_name(read_meta);
        fail_after = -1;
        if (st != SUF2_OK && read_meta == SUF2_OK && meta == SUF2_META_PENDING) {
            st = suf2_store_install(&store, manifest, &policy);
            retry = suf2_status_name(st);
        }
        unchanged = memcmp(snapshot, flash.data + store_cfg.staging_offset, capacity) == 0;
        free(snapshot);
        if (st == SUF2_OK) {
            ram_t installed = {flash.data + store_cfg.app_offset, session.image_length};
            if (tamper >= 0 && (uint32_t)tamper < installed.size) installed.data[tamper] ^= 0x01;
            suf2_reader_t reader = {&installed, ram_read};
            suf2_policy_t boot_policy = policy;
            boot_policy.decryption_keys = NULL;
            boot_policy.decryption_key_count = 0;
            boot = suf2_status_name(suf2_verify_boot(session.manifest, &boot_policy, &reader));
            if (installed_out) {
                FILE *out = fopen(installed_out, "wb");
                if (!out) { perror(installed_out); return 2; }
                bool saved = fwrite(installed.data, 1, installed.size, out) == installed.size;
                if (fclose(out) || !saved) { fprintf(stderr, "could not save installed image\n"); return 2; }
            }
        }
    }
    printf("{\"results\":\"%s\",\"reports\":[%s],\"state\":\"%s\",\"error\":\"%s\","
           "\"verify\":\"%s\",\"boot\":\"%s\",\"install\":\"%s\",\"retry\":\"%s\","
           "\"meta\":\"%s\",\"mutations\":%u,\"staging_unchanged\":%s}\n",
           results, reports, states[session.state], suf2_status_name(session.error), verify, boot,
           install, retry, meta_name, install_mutations, unchanged ? "true" : "false");
    free(flash.data);
    free(bitmap);
    free(erased);
    return 0;
}

/*
 * Native harness for the SUF2 core. Feeds a stream of 512-byte sectors into a
 * session backed by RAM staging, verifies the candidate, simulates installing
 * it and runs boot verification. Prints one JSON object.
 *
 *   suf2_harness --stream FILE --product HEX32 --base ADDR --key PUBHEX65
 *                [--key ...] [--max-size N] [--capacity N] [--bitmap-bytes N]
 *                [--require-encryption] [--tamper-installed OFFSET]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "suf2/suf2.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
} ram_t;

static suf2_status_t ram_write(void *ctx, uint32_t index, const uint8_t data[SUF2_CHUNK_SIZE])
{
    ram_t *r = ctx;
    if ((uint64_t)index * SUF2_CHUNK_SIZE + SUF2_CHUNK_SIZE > r->size) return SUF2_E_STORAGE;
    memcpy(r->data + (size_t)index * SUF2_CHUNK_SIZE, data, SUF2_CHUNK_SIZE);
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
    suf2_policy_t policy = {0};
    uint32_t capacity = 64 * 1024, bitmap_bytes = 64;
    long tamper = -1;
    policy.max_image_size = 64 * 1024;
    policy.keys = keys;

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
        else if (!strcmp(a, "--key")) {
            if (policy.key_count == 4 || hex_bytes(v, keys[policy.key_count].public_key, 65)) usage();
            policy.key_count++;
        } else usage();
    }
    if (!stream_path) usage();

    FILE *f = fopen(stream_path, "rb");
    if (!f) { perror(stream_path); return 2; }

    ram_t staging = {calloc(1, capacity), capacity};
    uint8_t *bitmap = calloc(1, bitmap_bytes);
    suf2_staging_t port = {&staging, ram_write, ram_read};
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
    const char *verify = "-", *boot = "-";
    if (session.state == SUF2_STATE_COMPLETE) {
        suf2_status_t st = suf2_verify_candidate(&session, &policy);
        verify = suf2_status_name(st);
        if (st == SUF2_OK) {
            /* Signed-only: the installed plaintext is the stored image. */
            ram_t installed = {malloc(session.image_length), session.image_length};
            memcpy(installed.data, staging.data, session.image_length);
            if (tamper >= 0 && (uint32_t)tamper < installed.size) installed.data[tamper] ^= 0x01;
            suf2_reader_t reader = {&installed, ram_read};
            boot = suf2_status_name(suf2_verify_boot(session.manifest, &policy, &reader));
            free(installed.data);
        }
    }
    printf("{\"results\":\"%s\",\"reports\":[%s],\"state\":\"%s\",\"error\":\"%s\",\"verify\":\"%s\",\"boot\":\"%s\"}\n",
           results, reports, states[session.state], suf2_status_name(session.error), verify, boot);
    free(staging.data);
    free(bitmap);
    return 0;
}

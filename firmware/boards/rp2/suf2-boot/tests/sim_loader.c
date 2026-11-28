/*
 * Host simulation of the RP2 loader's storage and USB drive code. It links
 * the real flash_store.c and msc_disk.c against a simulated NOR flash kept in
 * a file, so tests can run several "boots" and cut power at any flash
 * operation.
 *
 *   sim_loader --flash FILE --product HEX32 --key PUBHEX [--key ...]
 *              [--power-cut N] COMMAND
 *
 * Commands:
 *   copy SUF2FILE   write the file through the USB write callback, the way a
 *                   host would, then verify and install like the loader does
 *   boot            what the loader does at reset: resume or boot-verify
 *   disk IMAGE      save every sector of the virtual drive to IMAGE
 *
 * --power-cut N stops the Nth erase/program during install, half done, and
 * exits as if power was lost. Output is one JSON object.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flash_store.h"
#include "loader.h"
#include "suf2_board_config.h"
#include "tusb.h"

#define APP_BASE (0x10000000u + SUF2_APP_OFFSET)
#define MAX_BLOCKS (SUF2_SLOT_SIZE / SUF2_CHUNK_SIZE + 1u)
#define MAX_KEYS 4

uint8_t sim_flash[SUF2_FLASH_SIZE];

static long power_cut = -1;
static long flash_ops;
static bool armed; /* count only the install's flash operations */
static jmp_buf power_lost;
static suf2_session_t session;
static uint8_t bitmap[(MAX_BLOCKS + 7u) / 8u];
static suf2_signing_key_t keys[MAX_KEYS];
static suf2_policy_t policy = {.max_image_size = SUF2_SLOT_SIZE, .keys = keys};

static bool cut_next(void)
{
    return armed && power_cut >= 0 && flash_ops + 1 == power_cut;
}

static void count_op(void)
{
    if (armed && power_cut >= 0 && ++flash_ops == power_cut) longjmp(power_lost, 1);
}

void flash_range_erase(uint32_t offset, size_t count)
{
    if (cut_next()) memset(sim_flash + offset, 0xff, count / 2);
    count_op();
    memset(sim_flash + offset, 0xff, count);
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count)
{
    size_t n = cut_next() ? count / 2 : 0;
    for (size_t i = 0; i < n; i++) sim_flash[offset + i] &= data[i];
    count_op();
    for (size_t i = 0; i < count; i++) sim_flash[offset + i] &= data[i]; /* NOR: only clears bits */
}

bool tud_msc_set_sense(uint8_t lun, uint8_t key, uint8_t asc, uint8_t ascq)
{
    (void)lun, (void)key, (void)asc, (void)ascq;
    return true;
}

void loader_on_sector(const uint8_t sector[512])
{
    (void)suf2_session_write(&session, sector, NULL);
}

void loader_on_unmount(void) {}

size_t loader_info_text(char *buf, size_t size)
{
    return (size_t)snprintf(buf, size, "SUF2 loader simulation\r\n");
}

size_t loader_status_text(char *buf, size_t size)
{
    return (size_t)snprintf(buf, size, "Session state %d, %u of %u blocks\r\n", (int)session.state,
                            (unsigned)session.received, (unsigned)session.num_blocks);
}

static int hex_bytes(const char *s, uint8_t *out, size_t n)
{
    if (!s || strlen(s) != 2 * n) return -1;
    for (size_t i = 0; i < n; i++)
        if (sscanf(s + 2 * i, "%2hhx", &out[i]) != 1) return -1;
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "bad arguments; see the comment at the top of sim_loader.c\n");
    exit(2);
}

static void save_flash(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(sim_flash, 1, sizeof sim_flash, f) != sizeof sim_flash) exit(3);
    fclose(f);
}

static const char *meta_name(meta_state_t m)
{
    return m == META_COMMITTED ? "COMMITTED" : m == META_PENDING ? "PENDING" : "EMPTY";
}

static int cmd_copy(const char *path)
{
    static uint8_t file[4u << 20];
    FILE *f = fopen(path, "rb");
    if (!f) usage();
    size_t len = fread(file, 1, sizeof file, f);
    fclose(f);

    const suf2_session_config_t cfg = {&flash_staging, SUF2_SLOT_SIZE, bitmap, sizeof bitmap};
    if (suf2_session_init(&session, &cfg) != SUF2_OK) return 3;
    flash_store_session_reset();

    /* Like a host: some FAT metadata first, then the file in 4 KB clusters. */
    uint8_t meta[512];
    memset(meta, 0xA5, sizeof meta);
    tud_msc_write10_cb(0, 1, 0, meta, sizeof meta);
    for (size_t off = 0; off < len; off += 4096) {
        size_t n = len - off < 4096 ? len - off : 4096;
        tud_msc_write10_cb(0, (uint32_t)(1000 + off / 512), 0, file + off, (uint32_t)n);
    }
    tud_msc_write10_cb(0, 65, 0, meta, sizeof meta);

    printf("{\"state\":%d,\"error\":\"%s\"", (int)session.state, suf2_status_name(session.error));
    if (session.state != SUF2_STATE_COMPLETE) {
        printf("}\n");
        return 0;
    }
    suf2_status_t st = suf2_verify_candidate(&session, &policy);
    printf(",\"verify\":\"%s\"", suf2_status_name(st));
    if (st == SUF2_OK) {
        if (setjmp(power_lost)) {
            printf(",\"install\":\"POWER_CUT\",\"ops\":%ld}\n", flash_ops);
            return 0;
        }
        armed = true;
        st = flash_store_install(session.manifest, &policy);
        armed = false;
        printf(",\"install\":\"%s\"", suf2_status_name(st));
    }
    printf("}\n");
    return 0;
}

static int cmd_boot(void)
{
    uint8_t manifest[SUF2_MANIFEST_SIZE];
    meta_state_t m;
    suf2_status_t read_status = flash_store_read_meta(manifest, &m);
    if (read_status != SUF2_OK) {
        printf("{\"read_meta\":\"%s\"}\n", suf2_status_name(read_status));
        return 0;
    }
    printf("{\"meta\":\"%s\"", meta_name(m));
    if (m == META_PENDING) {
        if (setjmp(power_lost)) {
            printf(",\"resume\":\"POWER_CUT\"}\n");
            return 0;
        }
        armed = true;
        suf2_status_t st = flash_store_install(manifest, &policy);
        armed = false;
        printf(",\"resume\":\"%s\"", suf2_status_name(st));
        read_status = flash_store_read_meta(manifest, &m);
        if (read_status != SUF2_OK) {
            printf(",\"read_meta\":\"%s\"}\n", suf2_status_name(read_status));
            return 0;
        }
        printf(",\"meta_after\":\"%s\"", meta_name(m));
    }
    if (m == META_COMMITTED) {
        suf2_reader_t app = flash_store_app_reader();
        printf(",\"boot\":\"%s\"", suf2_status_name(suf2_verify_boot(manifest, &policy, &app)));
    }
    printf("}\n");
    return 0;
}

static int cmd_disk(const char *path)
{
    uint32_t count;
    uint16_t size;
    uint8_t sector[512];
    tud_msc_capacity_cb(0, &count, &size);
    FILE *f = fopen(path, "wb");
    if (!f) usage();
    for (uint32_t lba = 0; lba < count; lba++) {
        tud_msc_read10_cb(0, lba, 0, sector, sizeof sector);
        fwrite(sector, 1, sizeof sector, f);
    }
    fclose(f);
    printf("{\"sectors\":%u}\n", (unsigned)count);
    return 0;
}

int main(int argc, char **argv)
{
    const char *flash_path = NULL;
    int i = 1;
    policy.execution_base = APP_BASE;
    for (; i + 1 < argc && argv[i][0] == '-'; i += 2) {
        const char *a = argv[i], *v = argv[i + 1];
        if (!strcmp(a, "--flash")) flash_path = v;
        else if (!strcmp(a, "--product")) { if (hex_bytes(v, policy.product_id, SUF2_ID_SIZE)) usage(); }
        else if (!strcmp(a, "--power-cut")) power_cut = strtol(v, NULL, 0);
        else if (!strcmp(a, "--key")) {
            if (policy.key_count == MAX_KEYS || hex_bytes(v, keys[policy.key_count].public_key, SUF2_PUBLIC_KEY_SIZE))
                usage();
            policy.key_count++;
        } else usage();
    }
    if (!flash_path || i >= argc) usage();

    memset(sim_flash, 0xff, sizeof sim_flash);
    FILE *f = fopen(flash_path, "rb");
    if (f) {
        if (fread(sim_flash, 1, sizeof sim_flash, f) != sizeof sim_flash) exit(3);
        fclose(f);
    }

    suf2_status_t init_status = flash_store_init();
    if (init_status == SUF2_OK) init_status = msc_disk_init();
    if (init_status != SUF2_OK) {
        fprintf(stderr, "storage initialization failed: %s\n", suf2_status_name(init_status));
        return 3;
    }
    int rc;
    if (!strcmp(argv[i], "copy") && i + 1 < argc) rc = cmd_copy(argv[i + 1]);
    else if (!strcmp(argv[i], "boot")) rc = cmd_boot();
    else if (!strcmp(argv[i], "disk") && i + 1 < argc) rc = cmd_disk(argv[i + 1]);
    else usage();
    save_flash(flash_path);
    return rc;
}

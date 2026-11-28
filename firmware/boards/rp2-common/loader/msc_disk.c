/*
 * A read-only-looking FAT16 drive, built on the fly. It holds two text files
 * and nothing else. Every sector the host writes, wherever it lands, goes to
 * the SUF2 session; the session ignores anything that is not a SUF2 block, so
 * file system metadata written by the host is harmless.
 */
#include <string.h>

#include "tusb.h"

#include "loader.h"
#include "suf2_board_config.h"

#define SECTOR_SIZE 512u
#define SECTORS_PER_CLUSTER 8u
#define CLUSTERS 8000u /* enough to make this FAT16, not FAT12 */
#define RESERVED_SECTORS 1u
#define FAT_COUNT 2u
#define FAT_SECTORS 32u /* (CLUSTERS + 2) * 2 bytes, rounded up */
#define ROOT_ENTRIES 64u
#define ROOT_SECTORS (ROOT_ENTRIES * 32u / SECTOR_SIZE)
#define FAT_LBA RESERVED_SECTORS
#define ROOT_LBA (FAT_LBA + FAT_COUNT * FAT_SECTORS)
#define DATA_LBA (ROOT_LBA + ROOT_SECTORS)
#define TOTAL_SECTORS (DATA_LBA + CLUSTERS * SECTORS_PER_CLUSTER)
#define FILE_MAX (SECTORS_PER_CLUSTER * SECTOR_SIZE)

_Static_assert((CLUSTERS + 2u) * 2u <= FAT_SECTORS * SECTOR_SIZE, "FAT too small");
_Static_assert(TOTAL_SECTORS < 0x10000u, "use the 16-bit sector count");

#define FAT_DATE ((uint16_t)(((2025u - 1980u) << 9) | (1u << 5) | 1u))

typedef struct __attribute__((packed)) {
    char name[11];
    uint8_t attr;
    uint8_t nt_reserved;
    uint8_t ctime_tenth;
    uint16_t ctime;
    uint16_t cdate;
    uint16_t adate;
    uint16_t cluster_hi;
    uint16_t mtime;
    uint16_t mdate;
    uint16_t cluster_lo;
    uint32_t size;
} dir_entry_t;

_Static_assert(sizeof(dir_entry_t) == 32, "FAT directory entry is 32 bytes");

typedef size_t (*render_fn)(char *buf, size_t size);

static const struct {
    char name[11];
    render_fn render;
} files[] = {
    {"SUF2INFOTXT", loader_info_text},
    {"STATUS  TXT", loader_status_text},
};

#define FILE_COUNT (sizeof files / sizeof files[0])

static char text[FILE_MAX];
static uint8_t write_buf[SECTOR_SIZE];

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void boot_sector(uint8_t *s)
{
    static const uint8_t head[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'W', 'I', 'N', '4', '.', '1'};
    memcpy(s, head, sizeof head);
    put16(s + 11, SECTOR_SIZE);
    s[13] = SECTORS_PER_CLUSTER;
    put16(s + 14, RESERVED_SECTORS);
    s[16] = FAT_COUNT;
    put16(s + 17, ROOT_ENTRIES);
    put16(s + 19, TOTAL_SECTORS);
    s[21] = 0xF8;
    put16(s + 22, FAT_SECTORS);
    put16(s + 24, 1);  /* sectors per track */
    put16(s + 26, 1);  /* heads */
    s[36] = 0x80;      /* drive number */
    s[38] = 0x29;      /* extended boot signature */
    memcpy(s + 39, "\x32\x46\x55\x53", 4); /* volume serial */
    memcpy(s + 43, "SUF2BOOT   ", 11);
    memcpy(s + 54, "FAT16   ", 8);
    s[510] = 0x55;
    s[511] = 0xAA;
}

static void fat_sector(uint32_t index, uint8_t *s)
{
    if (index != 0) return;
    put16(s + 0, 0xFFF8); /* media descriptor */
    put16(s + 2, 0xFFFF);
    for (uint32_t i = 0; i < FILE_COUNT; i++) put16(s + 4 + 2 * i, 0xFFFF); /* one cluster each */
}

static void root_sector(uint32_t index, uint8_t *s)
{
    if (index != 0) return;
    dir_entry_t *d = (dir_entry_t *)s;
    memcpy(d[0].name, "SUF2BOOT   ", 11);
    d[0].attr = 0x08; /* volume label */
    d[0].mdate = FAT_DATE;
    for (uint32_t i = 0; i < FILE_COUNT; i++) {
        dir_entry_t *e = &d[1 + i];
        memcpy(e->name, files[i].name, 11);
        e->attr = 0x01; /* read-only */
        e->cdate = e->adate = e->mdate = FAT_DATE;
        e->cluster_lo = (uint16_t)(2 + i);
        e->size = (uint32_t)files[i].render(text, sizeof text);
    }
}

static void data_sector(uint32_t index, uint8_t *s)
{
    uint32_t cluster = index / SECTORS_PER_CLUSTER;
    uint32_t within = (index % SECTORS_PER_CLUSTER) * SECTOR_SIZE;
    if (cluster >= FILE_COUNT) return;
    size_t len = files[cluster].render(text, sizeof text);
    if (within < len) memcpy(s, text + within, len - within < SECTOR_SIZE ? len - within : SECTOR_SIZE);
}

static void render_sector(uint32_t lba, uint8_t *s)
{
    memset(s, 0, SECTOR_SIZE);
    if (lba == 0) boot_sector(s);
    else if (lba < ROOT_LBA) fat_sector((lba - FAT_LBA) % FAT_SECTORS, s);
    else if (lba < DATA_LBA) root_sector(lba - ROOT_LBA, s);
    else if (lba < TOTAL_SECTORS) data_sector(lba - DATA_LBA, s);
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id, "SUF2    ", 8);
    memcpy(product_id, "SUF2 Loader     ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;
    *block_count = TOTAL_SECTORS;
    *block_size = SECTOR_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
    (void)lun;
    (void)power_condition;
    if (load_eject && !start) loader_on_unmount();
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize)
{
    (void)lun;
    static uint8_t sector[SECTOR_SIZE];
    uint8_t *out = buffer;
    for (uint32_t done = 0; done < bufsize;) {
        uint32_t pos = offset + done;
        uint32_t in = pos % SECTOR_SIZE;
        uint32_t n = SECTOR_SIZE - in < bufsize - done ? SECTOR_SIZE - in : bufsize - done;
        render_sector(lba + pos / SECTOR_SIZE, sector);
        memcpy(out + done, sector + in, n);
        done += n;
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    (void)lba; /* SUF2 blocks are self-describing; where they land does not matter */
    for (uint32_t done = 0; done < bufsize;) {
        uint32_t in = (offset + done) % SECTOR_SIZE;
        uint32_t n = SECTOR_SIZE - in < bufsize - done ? SECTOR_SIZE - in : bufsize - done;
        memcpy(write_buf + in, buffer + done, n);
        done += n;
        if (in + n == SECTOR_SIZE) loader_on_sector(write_buf);
    }
    return (int32_t)bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;
    if (scsi_cmd[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) return 0;
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
    return -1;
}

void tud_umount_cb(void)
{
    loader_on_unmount();
}

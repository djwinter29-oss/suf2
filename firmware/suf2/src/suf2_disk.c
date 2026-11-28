/*
 * A read-only-looking FAT16 drive, built on the fly. It holds two text files
 * and nothing else. Every sector the host writes, wherever it lands, goes to
 * the SUF2 session; the session ignores anything that is not a SUF2 block, so
 * file system metadata written by the host is harmless.
 */
#include <string.h>

#include "suf2/suf2_disk.h"

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

_Static_assert((CLUSTERS + 2u) * 2u <= FAT_SECTORS * SECTOR_SIZE, "FAT too small");
_Static_assert(TOTAL_SECTORS < 0x10000u, "use the 16-bit sector count");
_Static_assert(TOTAL_SECTORS == SUF2_DISK_SECTOR_COUNT, "disk capacity mismatch");

#define FAT_DATE ((uint16_t)(((2025u - 1980u) << 9) | (1u << 5) | 1u))

static const char files[][11] = {"SUF2INFOTXT", "STATUS  TXT"};
#define FILE_COUNT (sizeof files / sizeof files[0])

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

static suf2_status_t render_text(suf2_disk_t *disk, uint32_t file, size_t *len)
{
    *len = file == 0 ? disk->cfg.info_text(disk->cfg.ctx, disk->text, sizeof disk->text) :
                      disk->cfg.status_text(disk->cfg.ctx, disk->text, sizeof disk->text);
    return *len < sizeof disk->text ? SUF2_OK : SUF2_E_ARGUMENT;
}

static suf2_status_t root_sector(suf2_disk_t *disk, uint32_t index, uint8_t *s)
{
    if (index != 0) return SUF2_OK;
    memcpy(s, "SUF2BOOT   ", 11);
    s[11] = 0x08; /* volume label */
    put16(s + 24, FAT_DATE);
    for (uint32_t i = 0; i < FILE_COUNT; i++) {
        uint8_t *e = s + 32 * (1 + i);
        size_t len;
        suf2_status_t st = render_text(disk, i, &len);
        if (st != SUF2_OK) return st;
        memcpy(e, files[i], 11);
        e[11] = 0x01;
        put16(e + 16, FAT_DATE);
        put16(e + 18, FAT_DATE);
        put16(e + 24, FAT_DATE);
        put16(e + 26, (uint16_t)(2 + i));
        for (unsigned j = 0; j < 4; j++) e[28 + j] = (uint8_t)(len >> (8 * j));
    }
    return SUF2_OK;
}

static suf2_status_t data_sector(suf2_disk_t *disk, uint32_t index, uint8_t *s)
{
    uint32_t cluster = index / SECTORS_PER_CLUSTER;
    uint32_t within = (index % SECTORS_PER_CLUSTER) * SECTOR_SIZE;
    if (cluster >= FILE_COUNT) return SUF2_OK;
    size_t len;
    suf2_status_t st = render_text(disk, cluster, &len);
    if (st != SUF2_OK) return st;
    if (within < len) memcpy(s, disk->text + within, len - within < SECTOR_SIZE ? len - within : SECTOR_SIZE);
    return SUF2_OK;
}

static suf2_status_t render_sector(suf2_disk_t *disk, uint32_t lba, uint8_t *s)
{
    memset(s, 0, SECTOR_SIZE);
    if (lba == 0) boot_sector(s);
    else if (lba < ROOT_LBA) fat_sector((lba - FAT_LBA) % FAT_SECTORS, s);
    else if (lba < DATA_LBA) return root_sector(disk, lba - ROOT_LBA, s);
    else if (lba < TOTAL_SECTORS) return data_sector(disk, lba - DATA_LBA, s);
    return SUF2_OK;
}

suf2_status_t suf2_disk_init(suf2_disk_t *disk, const suf2_disk_config_t *cfg)
{
    if (!disk || !cfg || !cfg->info_text || !cfg->status_text || !cfg->on_sector) return SUF2_E_ARGUMENT;
    disk->cfg = *cfg;
    memset(disk->write_buf, 0, sizeof disk->write_buf);
    return SUF2_OK;
}

suf2_status_t suf2_disk_read(suf2_disk_t *disk, uint32_t lba, uint32_t offset,
                             uint8_t *buffer, uint32_t size)
{
    if (!disk || (!buffer && size) || offset > UINT32_MAX - size ||
        lba >= TOTAL_SECTORS || (uint64_t)offset + size > (uint64_t)(TOTAL_SECTORS - lba) * SECTOR_SIZE)
        return SUF2_E_ARGUMENT;
    for (uint32_t done = 0; done < size;) {
        uint32_t pos = offset + done;
        uint32_t in = pos % SECTOR_SIZE;
        uint32_t n = SECTOR_SIZE - in < size - done ? SECTOR_SIZE - in : size - done;
        suf2_status_t st = render_sector(disk, lba + pos / SECTOR_SIZE, disk->sector);
        if (st != SUF2_OK) return st;
        memcpy(buffer + done, disk->sector + in, n);
        done += n;
    }
    return SUF2_OK;
}

suf2_status_t suf2_disk_write(suf2_disk_t *disk, uint32_t offset, const uint8_t *buffer, uint32_t size)
{
    if (!disk || (!buffer && size) || offset > UINT32_MAX - size) return SUF2_E_ARGUMENT;
    for (uint32_t done = 0; done < size;) {
        uint32_t in = (offset + done) % SECTOR_SIZE;
        uint32_t n = SECTOR_SIZE - in < size - done ? SECTOR_SIZE - in : size - done;
        memcpy(disk->write_buf + in, buffer + done, n);
        done += n;
        if (in + n == SECTOR_SIZE) disk->cfg.on_sector(disk->cfg.ctx, disk->write_buf);
    }
    return SUF2_OK;
}

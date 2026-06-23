/*
 * Declares the minimal PartOS FAT12/FAT16 service.
 *
 * This first assembly version intentionally does only a few tightly scoped
 * things:
 * - initialize one shared FAT worker thread,
 * - queue asynchronous mount requests as system-owned objects,
 * - parse enough FAT12/FAT16 metadata to describe a mounted volume,
 * - resolve one DOS 8.3 path to metadata through root/subdirectories.
 * - expose block-aligned file open/create/read/write on top of that metadata.
 *
 * The data-path API is intentionally block-oriented: reads and writes work in
 * 256-byte units to match both the block-device layer and the FAT volume
 * format used on this machine. That keeps the implementation very small while
 * still making mounted files genuinely usable.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef FAT_H
#define FAT_H

#include <stdint.h>

#include "dev.h"
#include "evt.h"

/*
 * PartOS block devices move data in 256-byte sectors. FAT boot sectors on
 * this machine must therefore also report 256 bytes per sector.
 */
#define FAT_SECTOR_SIZE         256
#define FAT_SHORT_NAME_LEN      11

/* directory attributes */
#define FAT_ATTR_READ_ONLY      0x01
#define FAT_ATTR_HIDDEN         0x02
#define FAT_ATTR_SYSTEM         0x04
#define FAT_ATTR_VOLUME_ID      0x08
#define FAT_ATTR_DIRECTORY      0x10
#define FAT_ATTR_ARCHIVE        0x20
#define FAT_ATTR_LFN            0x0f

/* completion / return codes */
#define FAT_OK                  0
#define FAT_ENOMEM             -1
#define FAT_EINVAL             -2
#define FAT_EBUSY              -3
#define FAT_ENODEV             -4
#define FAT_EIO                -5
#define FAT_ENOENT             -6
#define FAT_EEXIST             -7
#define FAT_ENOSPC             -8
#define FAT_EBADF              -9
#define FAT_ENOTSUP            -10
#define FAT_ENAMETOOLONG       -11
#define FAT_ECORRUPT           -12
#define FAT_EFBIG              -13
#define FAT_ENOTEMPTY          -14

/*
 * Mounted-volume descriptor filled by the FAT worker.
 *
 * `lba_base` is stored in device 256-byte sectors, not PC-style 512-byte
 * sectors. When a hard-disk partition table is present the worker probes the
 * partition start both ways and stores the base in the unit the driver uses.
 *
 * Layout must match src/os/fat.inc.
 */
typedef struct fat_fs_s {
    dev_t           *dev;               /* mounted device instance */
    uint16_t         lba_base;          /* partition base in 256-byte sectors */
    uint16_t         total_sectors;     /* FAT volume size in 256-byte sectors */
    uint16_t         reserved_sectors;  /* boot + reserved area */
    uint16_t         sectors_per_fat;   /* one FAT copy length */
    uint16_t         root_entries;      /* fixed root dir entries */
    uint16_t         root_dir_sectors;  /* root dir length in sectors */
    uint16_t         fat_start;         /* reserved_sectors */
    uint16_t         root_start;        /* first fixed-root sector */
    uint16_t         data_start;        /* first data-cluster sector */
    uint16_t         total_clusters;    /* derived cluster count */
    uint16_t         alloc_hint;        /* first free-cluster search hint */
    uint8_t          sectors_per_cluster; /* raw BPB sectors/cluster */
    uint8_t          num_fats;          /* number of FAT copies */
    uint8_t          fat_bits;          /* 12 or 16 */
    uint8_t          mounted;           /* 0 = invalid, 1 = ready */
    volatile int16_t status;            /* async completion status */
} fat_fs_t;

/*
 * Minimal lookup result returned by fat_lookup().
 *
 * `dir_sector` is the volume-relative sector that contains the owning 32-byte
 * directory entry; `dir_offset` is the byte offset of that entry inside the
 * sector. For the synthetic root-directory result (`"/"`), dir_sector is
 * 0xffff and dir_offset is 0.
 *
 * Layout must match src/os/fat.inc.
 */
typedef struct fat_dirent_s {
    uint16_t         first_cluster;     /* 0 for the FAT12/16 fixed root */
    uint32_t         size;              /* raw 32-bit directory size field */
    uint16_t         dir_sector;        /* volume-relative sector of entry */
    uint8_t          dir_offset;        /* byte offset of entry within sector */
    uint8_t          attr;              /* FAT_ATTR_* */
    volatile int16_t status;            /* async completion status */
} fat_dirent_t;

/*
 * Directory enumeration result returned by fat_readdir().
 *
 * The first 12 bytes match fat_dirent_t exactly so the worker can reuse the
 * same entry-fill path. Set `index` to the 0-based entry number before each
 * call and bump it by one to walk the directory; fat_readdir() returns
 * FAT_ENOENT once `index` is past the last entry. `name` is the raw 8.3 field
 * (11 bytes, space padded, no embedded dot, not NUL terminated).
 *
 * Layout must match src/os/fat.inc.
 */
typedef struct fat_dirinfo_s {
    uint16_t         first_cluster;     /* same layout as fat_dirent_t */
    uint32_t         size;              /* same layout as fat_dirent_t */
    uint16_t         dir_sector;        /* same layout as fat_dirent_t */
    uint8_t          dir_offset;        /* same layout as fat_dirent_t */
    uint8_t          attr;              /* FAT_ATTR_* (test FAT_ATTR_DIRECTORY) */
    volatile int16_t status;            /* async completion status */
    uint16_t         index;             /* in: 0-based entry to fetch */
    char             name[11];          /* out: raw 8.3 short name */
} fat_dirinfo_t;

/*
 * Open-file handle used by block-oriented file I/O.
 *
 * The first fields intentionally mirror fat_dirent_t exactly so the lookup
 * worker can populate this object directly during fat_open().
 *
 * `pos` is the current byte position. For fat_read()/fat_write() today both
 * `pos` and `size` must be 256-byte aligned; the file API is block-oriented,
 * not byte-stream oriented, in this first cut.
 *
 * Layout must match src/os/fat.inc.
 */
typedef struct fat_file_s {
    uint16_t         first_cluster;     /* same layout as fat_dirent_t */
    uint32_t         size;              /* same layout as fat_dirent_t */
    uint16_t         dir_sector;        /* same layout as fat_dirent_t */
    uint8_t          dir_offset;        /* same layout as fat_dirent_t */
    uint8_t          attr;              /* same layout as fat_dirent_t */
    volatile int16_t status;            /* same layout as fat_dirent_t */
    fat_fs_t        *fs;                /* owning mounted volume */
    uint32_t         pos;               /* current byte position */
} fat_file_t;

/*
 * Internal FAT bootstrap helper.
 *
 * Prepare the shared FAT worker and its internal events. Safe to call more
 * than once; later calls return FAT_OK once initialization succeeded.
 */
int16_t __fat_init(void);

/*
 * Queue one asynchronous mount request by direct device pointer or by device
 * name. The request completes when `event` becomes signaled and the final
 * result is also mirrored in `fs->status`.
 */
int16_t fat_mount_dev(fat_fs_t *fs, dev_t *dev, event_t *event);
int16_t fat_mount(fat_fs_t *fs, const char *dev_name, event_t *event);

/*
 * Queue one asynchronous read-only path lookup. Only DOS 8.3 names are
 * accepted today; the result lands in `entry` and completion is signaled by
 * `event`.
 */
int16_t fat_lookup(fat_fs_t *fs, const char *path, fat_dirent_t *entry, event_t *event);

/*
 * Enumerate one entry of the (currently fixed-root) directory named by `path`.
 * Set `info->index` to the 0-based entry number; the entry's metadata and raw
 * 8.3 name land in `info` and completion is signaled by `event`. Returns
 * FAT_ENOENT once `index` is past the last entry. This first cut lists the
 * root directory; pass "/" as the path.
 */
int16_t fat_readdir(fat_fs_t *fs, const char *path, fat_dirinfo_t *info, event_t *event);

/*
 * Resolve or create one path straight into an open-file handle, then transfer
 * file data in 256-byte blocks.
 *
 * Current constraints for size reasons:
 * - only regular files are accepted,
 * - file size, file position and transfer length must be 256-byte aligned.
 */
int16_t fat_open(fat_fs_t *fs, const char *path, fat_file_t *file, event_t *event);
int16_t fat_create(fat_fs_t *fs, const char *path, fat_file_t *file, event_t *event);
int16_t fat_read(fat_file_t *file, void *buf, uint16_t bytes, event_t *event);
int16_t fat_write(fat_file_t *file, const void *buf, uint16_t bytes, event_t *event);

/*
 * Mutating directory-tree operations. Each takes a fat_dirent_t result object
 * purely so the caller can wait on `event` and read `result->status`; mkdir
 * also fills `result` with the new directory's metadata. unlink removes one
 * regular file, rmdir removes one empty directory (FAT_ENOTEMPTY otherwise),
 * mkdir creates one directory seeded with "." and "..".
 */
int16_t fat_unlink(fat_fs_t *fs, const char *path, fat_dirent_t *result, event_t *event);
int16_t fat_mkdir(fat_fs_t *fs, const char *path, fat_dirent_t *result, event_t *event);
int16_t fat_rmdir(fat_fs_t *fs, const char *path, fat_dirent_t *result, event_t *event);

#endif /* FAT_H */

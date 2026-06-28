            ;; fat.inc
            ;;
            ;; shared FAT ABI/constants for the assembly implementation.
            ;;
            ;; this include is the contract between:
            ;;   - the public C header (include/fat.h)
            ;;   - the shared worker core (fat.s)
            ;;   - mount-time logic (fat_mount.s)
            ;;   - path lookup logic (fat_lookup.s)
            ;;
            ;; public structure offsets MUST stay in lock-step with fat.h.
            ;; private request/layout values are kept here so the split asm
            ;; files can share one exact view of queue nodes and scratch state.
            ;;
            ;; 2026-06-16   tstih

            ;; --- public return codes (16-bit signed) ------------------------
            .equ    FAT_OK,                 0x0000
            .equ    FAT_ENOMEM,             0xffff
            .equ    FAT_EINVAL,             0xfffe
            .equ    FAT_EBUSY,              0xfffd
            .equ    FAT_ENODEV,             0xfffc
            .equ    FAT_EIO,                0xfffb
            .equ    FAT_ENOENT,             0xfffa
            .equ    FAT_EEXIST,             0xfff9
            .equ    FAT_ENOSPC,             0xfff8
            .equ    FAT_EBADF,              0xfff7
            .equ    FAT_ENOTSUP,            0xfff6
            .equ    FAT_ENAMETOOLONG,       0xfff5
            .equ    FAT_ECORRUPT,           0xfff4
            .equ    FAT_EFBIG,              0xfff3
            .equ    FAT_ENOTEMPTY,          0xfff2

            ;; --- public constants -------------------------------------------
            .equ    FAT_SECTOR_SIZE,        256
            .equ    FAT_SHORT_NAME_LEN,     11
            .equ    FAT_CLUSTER_FIRST,      2
            .equ    FAT_FAT12_LIMIT,        0x0ff5
            .equ    FAT_ATTR_READ_ONLY,     0x01
            .equ    FAT_ATTR_VOLUME_ID,     0x08
            .equ    FAT_ATTR_DIRECTORY,     0x10
            .equ    FAT_ATTR_ARCHIVE,       0x20
            .equ    FAT_ATTR_LFN,           0x0f
            .equ    FAT_ENTRY_SIZE,         32
            .equ    FAT_ENTRIES_PER_SECTOR, 8
            .equ    FAT_ENTRY_ATTR,         11
            .equ    FAT_ENTRY_CLUSTER,      26
            .equ    FAT_ENTRY_SIZE32,       28
            .equ    FAT_ENTRY_DELETED,      0xe5
            .equ    FAT_ENTRY_END,          0x00
            .equ    FAT_DOT,                '.'
            .equ    FAT_SLASH,              '/'
            .equ    FAT_SPACE,              ' '

            ;; --- worker configuration ---------------------------------------
            ;; the worker's call chain is deep: fat_worker -> fat_handle_mount ->
            ;; fat_read_block -> fat_dev_call3 (pushes 4 words) -> fd_xfer_start ->
            ;; fd_dma_start_sector -> fd_seek_dev -> fd_wait_int -> fd_write_data.
            ;; worker + bootstrap stacks both live in the user heap; a sacrificial
            ;; guard block is allocated between them so a deep lookup cannot
            ;; scribble into the bootstrap stack. (user-heap only, not kernel.)
            .equ    FAT_USER_STACK_GUARD,   2048
            .equ    FAT_WORKER_STACK,       0

            ;; --- fat_fs_t offsets (30 bytes) --------------------------------
            .equ    FATFS_DEV,              0
            .equ    FATFS_LBA_BASE,         2
            .equ    FATFS_TOTAL_SECTORS,    4
            .equ    FATFS_RESERVED,         6
            .equ    FATFS_SECTORS_PER_FAT,  8
            .equ    FATFS_ROOT_ENTRIES,     10
            .equ    FATFS_ROOT_DIR_SECTORS, 12
            .equ    FATFS_FAT_START,        14
            .equ    FATFS_ROOT_START,       16
            .equ    FATFS_DATA_START,       18
            .equ    FATFS_TOTAL_CLUSTERS,   20
            .equ    FATFS_ALLOC_HINT,       22
            .equ    FATFS_SECTORS_PER_CL,   24
            .equ    FATFS_NUM_FATS,         25
            .equ    FATFS_FAT_BITS,         26
            .equ    FATFS_MOUNTED,          27
            .equ    FATFS_STATUS,           28
            .equ    FATFS_SIZE,             30

            ;; --- fat_dirent_t offsets (12 bytes) ----------------------------
            .equ    FATDIRENT_FIRST_CLUSTER,0
            .equ    FATDIRENT_SIZE,         2
            .equ    FATDIRENT_DIR_SECTOR,   6
            .equ    FATDIRENT_DIR_OFFSET,   8
            .equ    FATDIRENT_ATTR,         9
            .equ    FATDIRENT_STATUS,       10
            .equ    FATDIRENT_SIZEOF,       12

            ;; --- fat_dirinfo_t offsets (25 bytes) ---------------------------
            ;; readdir output. first 12 bytes match fat_dirent_t so the worker
            ;; reuses fat_fill_dirent_from_entry$. index (in) selects the entry;
            ;; name (out) is the raw 8.3 field (space-padded, no embedded dot).
            .equ    FATDIRINFO_INDEX,       12
            .equ    FATDIRINFO_NAME,        14
            .equ    FATDIRINFO_SIZEOF,      25

            ;; --- fat_file_t offsets (18 bytes) ------------------------------
            ;; first 12 bytes intentionally match fat_dirent_t exactly.
            .equ    FATFILE_FIRST_CLUSTER,  FATDIRENT_FIRST_CLUSTER
            .equ    FATFILE_SIZE,           FATDIRENT_SIZE
            .equ    FATFILE_DIR_SECTOR,     FATDIRENT_DIR_SECTOR
            .equ    FATFILE_DIR_OFFSET,     FATDIRENT_DIR_OFFSET
            .equ    FATFILE_ATTR,           FATDIRENT_ATTR
            .equ    FATFILE_STATUS,         FATDIRENT_STATUS
            .equ    FATFILE_FS,             12
            .equ    FATFILE_POS,            14
            .equ    FATFILE_SIZEOF,         18

            ;; --- internal queued request layout -----------------------------
            ;; request header starts with a sysobj-compatible next/owner pair
            ;; so later process cleanup can walk the queue by owner.
            .equ    FATREQ_NEXT,            0
            .equ    FATREQ_OWNER,           2
            .equ    FATREQ_OP,              4
            .equ    FATREQ_FLAGS,           5
            .equ    FATREQ_EVENT,           6
            .equ    FATREQ_FS,              8
            .equ    FATREQ_DEV,             10
            .equ    FATREQ_BUF,             12
            .equ    FATREQ_BYTES,           14
            .equ    FATREQ_ARG,             16
            .equ    FATREQ_SIZE,            18

            .equ    FATREQ_OP_MOUNT,        1
            .equ    FATREQ_OP_LOOKUP,       2
            .equ    FATREQ_OP_READ,         3
            .equ    FATREQ_OP_WRITE,        4
            .equ    FATREQ_OP_CREATE,       5
            .equ    FATREQ_OP_READDIR,      6
            .equ    FATREQ_OP_UNLINK,       7
            .equ    FATREQ_OP_MKDIR,        8
            .equ    FATREQ_OP_RMDIR,        9

            .equ    FATREQ_FLAG_FILE_ONLY,  0x01

            ;; --- scan helper return states ----------------------------------
            .equ    FAT_SCAN_CONTINUE,      0
            .equ    FAT_SCAN_FOUND,         1
            .equ    FAT_SCAN_END,           2
            .equ    FAT_WALK_LOOKUP,        0
            .equ    FAT_WALK_CREATE,        1
            .equ    FAT_WALK_READDIR,       2

            ;; --- boot sector offsets (FAT12/FAT16) --------------------------
            .equ    FATBOOT_BPS,            11
            .equ    FATBOOT_SPC,            13
            .equ    FATBOOT_RESERVED,       14
            .equ    FATBOOT_NUM_FATS,       16
            .equ    FATBOOT_ROOT_ENTRIES,   17
            .equ    FATBOOT_TOTAL16,        19
            .equ    FATBOOT_SECTORS_PER_FAT,22
            .equ    FATBOOT_TOTAL32,        32
            .equ    FATBOOT_SIG_LO,         254
            .equ    FATBOOT_SIG_HI,         255

            ;; --- mbr partition table offsets within block 1 -----------------
            ;; the disk uses 256-byte physical transfers, so the standard PC
            ;; 512-byte MBR spans device blocks 0 and 1. the partition table
            ;; (offset 0x1be) and signature (offset 0x1fe) therefore live in
            ;; the SECOND 256-byte block at the offsets below.
            .equ    FATMBR_PART0,           190
            .equ    FATMBR_PART_SIZE,       16
            .equ    FATMBR_TYPE,            4
            .equ    FATMBR_START,           8
            .equ    FATMBR_SIZE,            12
            .equ    FATMBR_SIG_LO,          254
            .equ    FATMBR_SIG_HI,          255

            ;; --- event / thread offsets pulled from kernel layout ----------
            .equ    THREAD_PROCESS,         22
            .equ    FAT_NONSIGNALED,        0
            .equ    FAT_SIGNALED,           1
            .equ    FAT_INIT_NONE,          0
            .equ    FAT_INIT_BUSY,          1
            .equ    FAT_INIT_READY,         2

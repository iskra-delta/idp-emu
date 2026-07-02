/*
 * Aggregated public PartOS API header.
 *
 * This header is the one-stop public ABI for callers that want to obtain the
 * exported PartOS function table through the named-service lookup bridge and
 * then drive kernel / OS services through that table.
 *
 * The structures below intentionally mirror the live assembly layouts used by
 * the current kernel and OS modules so the table can expose real pointers to
 * live kernel objects without wrappers or translation layers.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef PARTOS_H
#define PARTOS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PARTOS_SERVICE_NAME         "partos"
#define LIBC_SERVICE_NAME           "libc"
#define SHELL_SERVICE_NAME          LIBC_SERVICE_NAME

/*
 * Generic intrusive list / object model.
 */
typedef struct list_item_s {
    struct list_item_s *next;
    uint8_t data[0];
} list_item_t;

typedef list_item_t list_t;

typedef struct sysobj_s {
    union {
        list_item_t hdr;
        void *next;
    };
    void *owner;
} sysobj_t;

/*
 * Event API.
 */
typedef enum event_state_e {
    nonsignaled = 0,
    signaled    = 1
} event_state_t;

typedef struct event_s {
    sysobj_t hdr;
    uint8_t  state;
} event_t;

/*
 * Service API.
 */
#define MAX_SVC_NAME_LEN            16

typedef struct service_s {
    sysobj_t hdr;
    char     name[MAX_SVC_NAME_LEN];
    void    *fntable;
} service_t;

/*
 * Timer API.
 */
#define EVERYTIME                   0

typedef struct timer_s {
    sysobj_t hdr;
    void   (*hook)(void);
    uint16_t ticks;
    uint16_t _tick_count;
} timer_t;

/*
 * Device / driver API.
 */
#define DEV_MAX                     16

#define DEV_F_OPEN                  0x01
#define DEV_F_BUSY                  0x02
#define DEV_F_ERROR                 0x04
#define DEV_F_LOCKED                0x10

#define IOCTL_GETFLAGS              0x01
#define IOCTL_SETFLAGS              0x02
#define IOCTL_CLRFLAGS              0x03
#define IOCTL_GETPOS                0x10
#define IOCTL_SETPOS                0x11

struct dev_s;

typedef struct dev_drv_s {
    struct dev_drv_s *next;
    struct dev_s *(*probe)(void);
    int16_t       (*init)(void);
    int16_t       (*open)(struct dev_s *dev);
    void          (*close)(struct dev_s *dev);
    int16_t       (*read)(struct dev_s *dev, uint8_t *buf, uint16_t len, event_t *event);
    int16_t       (*write)(struct dev_s *dev, const uint8_t *buf, uint16_t len, event_t *event);
    int16_t       (*ioctl)(struct dev_s *dev, uint8_t cmd, void *params);
} dev_drv_t;

typedef struct dev_s {
    struct dev_s *next;
    uint8_t     name[6];
    uint8_t     flags;
    uint8_t     data[9];
    dev_drv_t  *driver;
} dev_t;

/*
 * Thread / process API.
 */
#define THREAD_STATE_SUSPENDED      0
#define THREAD_STATE_RUNNING        1
#define THREAD_STATE_WAITING        2
#define THREAD_STATE_JOINED         3
#define THREAD_STATE_TERMINATED     4

#define CONTEXT_SIZE                22

typedef struct thread_s {
    sysobj_t hdr;
    uint16_t sp;
    uint8_t  startup[10];
    event_t **wait;
    uint8_t  num_events;
    uint8_t  state;
    struct thread_s **joined;
    void    *process;
    uint8_t  bank;
    event_t *wait_inline;
} thread_t;

#define MAX_PNAME_LEN               8
#define PROCESS_INTERNAL            0x01

#define XL_HDR_SIZE                 12
#define XL_RELOC_SIZE               4
#define XL_OFF_MAGIC0               0
#define XL_OFF_MAGIC1               1
#define XL_OFF_VERSION              2
#define XL_OFF_FLAGS                3
#define XL_OFF_ENTRY                4
#define XL_OFF_CODE_SIZE            6
#define XL_OFF_RELOC_CNT            8

#ifndef PARTOS_XL_HEADER_DEFINED
#define PARTOS_XL_HEADER_DEFINED
typedef struct xl_header_s {
    char     magic[2];
    uint8_t  version;
    uint8_t  flags;
    uint16_t entry;
    uint16_t code_size;
    uint16_t reloc_count;
    uint16_t reserved;
} xl_header_t;
#endif

#define PROCESS_LOAD_OK             0
#define PROCESS_LOAD_ERR_NOT_FOUND  1
#define PROCESS_LOAD_ERR_ALLOC      2
#define PROCESS_LOAD_ERR_READ       3
#define PROCESS_LOAD_ERR_XL_INVALID 4
#define PROCESS_LOAD_ERR_XL_START   5
#define PROCESS_LOAD_ERR_COM_INVALID 6

#define COM_HDR_SIZE                16
#define COM_OFF_MAGIC0              0
#define COM_OFF_MAGIC1              1
#define COM_OFF_VERSION             2
#define COM_OFF_FLAGS               3
#define COM_OFF_STACK_SIZE          4
#define COM_OFF_ENTRY_HINT          6
#define COM_OFF_XL_OFFSET           8
#define COM_OFF_XL_SIZE             10
#define COM_OFF_RESERVED0           12
#define COM_OFF_RESERVED1           14

#ifndef PARTOS_COM_HEADER_DEFINED
#define PARTOS_COM_HEADER_DEFINED
typedef struct com_header_s {
    char     magic[2];
    uint8_t  version;
    uint8_t  flags;
    uint16_t stack_size;
    uint16_t entry_hint;
    uint16_t xl_offset;
    uint16_t xl_size;
    uint16_t reserved0;
    uint16_t reserved1;
} com_header_t;
#endif

typedef struct process_s {
    sysobj_t hdr;
    uint8_t  pflags;
    char     pname[MAX_PNAME_LEN];
    thread_t *main_thread;
    const char *cmdline;
    const char *environment;
} process_t;

/*
 * Memory API.
 */
#define BLK_SIZE                    (sizeof(struct block_s) - sizeof(uint8_t[1]))
#define MIN_CHUNK_SIZE              4
#define NEW                         0x00
#define ALLOCATED                   0x01

typedef struct block_s {
    sysobj_t hdr;
    uint8_t  stat;
    uint16_t size;
    uint8_t  data[1];
} block_t;

/*
 * Interrupt / vector API. Vector ids ARE the low-page rst/nmi slot addresses:
 * set_vector patches the `jp` operand in that slot directly (no table). rst 0x00
 * is the non-vectorable cold entry; the 50 Hz tick hook is reached by a software
 * call from the VBL ISR (not a rst), so it is installed separately.
 */
#define VECTOR_RST08                0x08
#define VECTOR_RST10                0x10
#define VECTOR_RST18                0x18
#define VECTOR_RST20                0x20
#define VECTOR_RST28                0x28
#define VECTOR_RST30                0x30
#define VECTOR_RST38                0x38
#define VECTOR_NMI                  0x66

/*
 * FAT API.
 */
#define FAT_SECTOR_SIZE             256
#define FAT_SHORT_NAME_LEN          11

#define FAT_ATTR_READ_ONLY          0x01
#define FAT_ATTR_HIDDEN             0x02
#define FAT_ATTR_SYSTEM             0x04
#define FAT_ATTR_VOLUME_ID          0x08
#define FAT_ATTR_DIRECTORY          0x10
#define FAT_ATTR_ARCHIVE            0x20
#define FAT_ATTR_LFN                0x0f

#define FAT_OK                      0
#define FAT_ENOMEM                 -1
#define FAT_EINVAL                 -2
#define FAT_EBUSY                  -3
#define FAT_ENODEV                 -4
#define FAT_EIO                    -5
#define FAT_ENOENT                 -6
#define FAT_EEXIST                 -7
#define FAT_ENOSPC                 -8
#define FAT_EBADF                  -9
#define FAT_ENOTSUP                -10
#define FAT_ENAMETOOLONG           -11
#define FAT_ECORRUPT               -12
#define FAT_EFBIG                  -13
#define FAT_ENOTEMPTY              -14

typedef struct fat_fs_s {
    dev_t           *dev;
    uint16_t         lba_base;
    uint16_t         total_sectors;
    uint16_t         reserved_sectors;
    uint16_t         sectors_per_fat;
    uint16_t         root_entries;
    uint16_t         root_dir_sectors;
    uint16_t         fat_start;
    uint16_t         root_start;
    uint16_t         data_start;
    uint16_t         total_clusters;
    uint16_t         alloc_hint;
    uint8_t          sectors_per_cluster;
    uint8_t          num_fats;
    uint8_t          fat_bits;
    uint8_t          mounted;
    volatile int16_t status;
} fat_fs_t;

typedef struct fat_dirent_s {
    uint16_t         first_cluster;
    uint32_t         size;
    uint16_t         dir_sector;
    uint8_t          dir_offset;
    uint8_t          attr;
    volatile int16_t status;
} fat_dirent_t;

typedef struct fat_dirinfo_s {
    uint16_t         first_cluster;
    uint32_t         size;
    uint16_t         dir_sector;
    uint8_t          dir_offset;
    uint8_t          attr;
    volatile int16_t status;
    uint16_t         index;
    char             name[11];
} fat_dirinfo_t;

typedef struct fat_file_s {
    uint16_t         first_cluster;
    uint32_t         size;
    uint16_t         dir_sector;
    uint8_t          dir_offset;
    uint8_t          attr;
    volatile int16_t status;
    fat_fs_t        *fs;
    uint32_t         pos;
} fat_file_t;

/*
 * Small device payloads commonly passed through generic device driver calls.
 */
#define NVRAM_BLOCK_SIZE            8

typedef struct nvram_block_s {
    uint8_t data[NVRAM_BLOCK_SIZE];
} nvram_block_t;

typedef struct partner_bios_nvram_s {
    uint8_t boot_csum;
    uint8_t fd_types;
    uint8_t dev_types;
    uint8_t ttys_attach;
    uint8_t ttys0_cfg;
    uint8_t ttys1_cfg;
    uint8_t ttys2_cfg;
    uint8_t ttys3_cfg;
} partner_bios_nvram_t;

#define RTC_TIME_SIZE               6

typedef struct minimal_rtc_time_s {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t mday;
    uint8_t mon;
    uint8_t year;
} minimal_rtc_time_t;

#define PARTOS_TEXT_ATTR_NORMAL     0x00
#define PARTOS_TEXT_ATTR_UNDERLINE  0x02
#define PARTOS_TEXT_ATTR_HIGHLIGHT  0x10
#define PARTOS_TEXT_ATTR_INVERSE    0x20
#define PARTOS_TEXT_ATTR_MASK       (PARTOS_TEXT_ATTR_UNDERLINE | \
                                     PARTOS_TEXT_ATTR_HIGHLIGHT | \
                                     PARTOS_TEXT_ATTR_INVERSE)

#define AVDC_ATTR_NORMAL            PARTOS_TEXT_ATTR_NORMAL
#define AVDC_ATTR_UNDERLINE         PARTOS_TEXT_ATTR_UNDERLINE
#define AVDC_ATTR_HIGHLIGHT         PARTOS_TEXT_ATTR_HIGHLIGHT
#define AVDC_ATTR_INVERSE           PARTOS_TEXT_ATTR_INVERSE
#define AVDC_ATTR_MASK              PARTOS_TEXT_ATTR_MASK

#define AVDC_CURSOR_HIDE            0
#define AVDC_CURSOR_SHOW            1

#define AVDC_IOCTL_SETATTR          0x20
#define AVDC_IOCTL_CLEAR            0x21
#define AVDC_IOCTL_CURSOR           0x22
#define AVDC_IOCTL_GOTOXY           0x23

typedef struct avdc_xy_s {
    uint8_t x;
    uint8_t y;
} avdc_xy_t;

#define SIO_IOCTL_SETBUFS           0x20
#define SIO_IOCTL_LOCK              0x21
#define SIO_IOCTL_UNLOCK            0x22
#define SIO_IOCTL_INITLINE          0x23

#define SIO_PARITY_NONE             0
#define SIO_PARITY_EVEN             1
#define SIO_PARITY_ODD              2

typedef struct sio_bufcfg_s {
    uint8_t rx_ring_size;
    uint8_t tx_queue_size;
} sio_bufcfg_t;

typedef struct sio_linecfg_s {
    uint16_t baud;
    uint8_t  data_bits;
    uint8_t  parity;
    uint8_t  stop_bits;
} sio_linecfg_t;

#define PIO_IOCTL_SETBUFS           0x20
#define PIO_IOCTL_LOCK              0x21
#define PIO_IOCTL_UNLOCK            0x22
#define PIO_IOCTL_SETMODE           0x23

#define PIO_MODE_OUTPUT             0
#define PIO_MODE_INPUT              1

typedef struct pio_bufcfg_s {
    uint8_t rx_ring_size;
    uint8_t tx_queue_size;
} pio_bufcfg_t;

typedef struct pio_modecfg_s {
    uint8_t mode;
} pio_modecfg_t;

/*
 * Live kernel snapshot returned by partos_get_sys_info().
 *
 * Layout must match the dedicated shared page-0 sys-info block in
 * src/kernel/page0.s.
 */
typedef struct sys_info_s {
    uint8_t    nvram_cache[NVRAM_BLOCK_SIZE];
    uint8_t    version;
    uint8_t    model;
    uint8_t    flags;
    uint8_t    meta1;
    dev_drv_t *first_driver;
    dev_t     *first_device;
    service_t *first_service;
    process_t *first_process;
    event_t   *first_event;
    timer_t   *first_timer;
    thread_t  *current_thread;
    thread_t  *first_running_thread;
    thread_t  *first_waiting_thread;
    thread_t  *first_suspended_thread;
    thread_t  *first_terminated_thread;
    void      *system_heap;
    void      *user_heap;
} sys_info_t;

/*
 * Exported PartOS syscall table.
 *
 * The named-service lookup bridge returns a pointer to this exact structure.
 * The field order must therefore stay in sync with src/os/syscall.s.
 */
typedef struct partos_s {
    sys_info_t *(*get_sys_info)(void);

    int16_t (*clear_screen)(void);
    int16_t (*set_xy)(const avdc_xy_t *xy);
    int16_t (*write_console)(const void *buf, uint16_t len);
    int16_t (*peek_keyboard)(void);
    int16_t (*read_keyboard)(void);
    process_t *(*run_command)(const char *name);

    list_item_t *(*find_list_item)(
        list_item_t *first,
        list_item_t **prev,
        uint8_t (*match)(list_item_t *p, uint16_t arg),
        uint16_t the_arg);
    void (*iterate_list)(
        list_item_t *first,
        void (*fn)(list_item_t *p, uint16_t arg),
        uint16_t the_arg);
    list_item_t *(*insert_list_item)(list_item_t **first, list_item_t *el);
    list_item_t *(*remove_list_item)(list_item_t **first, list_item_t *el);
    list_item_t *(*append_list_item)(list_item_t **first, list_item_t *chain);

    void (*initialize_memory_region)(void *heap, uint16_t size);
    void *(*allocate_memory)(void *heap, uint16_t size, void *owner);
    void *(*deallocate_memory)(void *heap, void *p);

    void (*disable_interrupts)(void);
    void (*enable_interrupts)(void);
    void *(*set_interrupt_handler)(uint8_t vector, void *handler);
    void *(*set_vector_handler)(uint8_t vector, void *handler);

    service_t *(*register_service)(const char *name, void *fntable);
    service_t *(*unregister_service)(service_t *service);
    void *(*query_service)(const char *name);

    event_t *(*create_event)(void *owner);
    event_t *(*destroy_event)(event_t *event);
    event_t *(*set_event)(event_t *event, uint8_t new_state);
    timer_t *(*install_timer)(void (*hook)(void), uint16_t ticks, void *owner);
    timer_t *(*uninstall_timer)(timer_t *timer);

    /* ABI note: this is the raw kernel-backed thread entry, so callers must
     * pass the target RAM bank explicitly even though the higher-level process
     * helpers usually keep this at bank 1 today. */
    thread_t *(*create_thread)(void (*entry_point)(void), uint16_t stack_size,
                               uint8_t bank, void *process);
    void (*resume_thread)(thread_t *thread);
    void (*suspend_thread)(thread_t *thread);
    void (*exit_thread)(thread_t *thread);
    void (*wait_for_events)(event_t **events, uint8_t num_events);

    process_t *(*start_process)(const char *pname, void (*entry_point)(void), uint16_t stack_size);
    process_t *(*load_process_image)(const char *pname, uint8_t *img, uint16_t img_size, uint16_t stack_size);
    process_t *(*load_process_com)(const char *pname, uint8_t *img, uint16_t img_size);
    void (*exit_process)(void);

    int16_t (*mount_filesystem_by_device)(fat_fs_t *fs, dev_t *dev, event_t *event);
    int16_t (*mount_filesystem_by_name)(fat_fs_t *fs, const char *dev_name, event_t *event);
    int16_t (*lookup_path)(fat_fs_t *fs, const char *path, fat_dirent_t *entry, event_t *event);
    int16_t (*open_file)(fat_fs_t *fs, const char *path, fat_file_t *file, event_t *event);
    int16_t (*create_file)(fat_fs_t *fs, const char *path, fat_file_t *file, event_t *event);
    int16_t (*read_file)(fat_file_t *file, void *buf, uint16_t bytes, event_t *event);
    int16_t (*write_file)(fat_file_t *file, const void *buf, uint16_t bytes, event_t *event);
    int16_t (*read_directory)(fat_fs_t *fs, const char *path, fat_dirinfo_t *info, event_t *event);
    fat_fs_t *(*get_boot_filesystem)(void);
    const char *(*get_command_line)(void);
    char *(*get_current_dir)(void);
    int16_t (*unlink_path)(fat_fs_t *fs, const char *path, fat_dirent_t *result, event_t *event);
    int16_t (*mkdir_path)(fat_fs_t *fs, const char *path, fat_dirent_t *result, event_t *event);
    int16_t (*rmdir_path)(fat_fs_t *fs, const char *path, fat_dirent_t *result, event_t *event);
    int16_t (*wait_process)(process_t *process);
    /* Appended to preserve all existing offsets in the live service table. */
    int16_t (*set_text_attr)(uint8_t attr);
} partos_t;

/*
 * Optional shell-owned libc service table.
 *
 * The interactive shell may register this under LIBC_SERVICE_NAME while it is
 * alive. The first entries cover process launch metadata in a shell-owned but
 * process-addressable way; the trailing entries are shell presentation helpers
 * that remain useful to interactive tools.
 */
typedef struct libc_s {
    const char *(*get_command_line)(void);
    const char *(*get_environment)(void);
    const char *(*getenv)(const char *name);
    const char *(*get_current_device_name)(void);
    int16_t (*write_prompt)(void);
    /* Appended to preserve the original shell-service offsets. */
    uint16_t (*strlen)(const char *s);
    int16_t (*strcmp)(const char *lhs, const char *rhs);
    int16_t (*strncmp)(const char *lhs, const char *rhs, uint16_t n);
    char *(*strcpy)(char *dst, const char *src);
    void *(*memcpy)(void *dst, const void *src, uint16_t n);
    void *(*memset)(void *dst, int16_t value, uint16_t n);
    int16_t (*write)(const void *buf, uint16_t len);
    int16_t (*putchar)(int16_t ch);
    int16_t (*puts)(const char *s);
} libc_t;

typedef libc_t shell_t;

/*
 * Registered named service and its exported PartOS function table.
 */
extern service_t *syscall_service;
extern partos_t   syscall_table;

/*
 * Register the named PartOS service once and return its service object.
 */
service_t *syscall_init(void);

/*
 * Refresh and return the live kernel system-information snapshot.
 */
sys_info_t *partos_get_sys_info(void);

#ifdef __cplusplus
}
#endif

#endif /* PARTOS_H */

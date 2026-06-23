/*
 * Declares the PartOS device model shared by kernel code and hardware
 * drivers, including device instances, driver descriptors, and common
 * ioctl flags.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef DEV_H
#define DEV_H

#include <stdint.h>

/* Maximum number of device instances. */
#define DEV_MAX         16

/* dev_s.flags bits */
#define DEV_F_OPEN      0x01    /* instance is open */
#define DEV_F_BUSY      0x02    /* operation in progress */
#define DEV_F_ERROR     0x04    /* last operation failed */
#define DEV_F_LOCKED    0x10    /* locked against concurrent access */

/* Standard ioctl commands. */
#define IOCTL_GETFLAGS  0x01    /* output(de): current flags */
#define IOCTL_SETFLAGS  0x02    /* input(de): flags to set */
#define IOCTL_CLRFLAGS  0x03    /* input(de): flags to clear */

/* Shared flat-block byte-position ioctls (24-bit little-endian offset). */
#define IOCTL_GETPOS    0x10
#define IOCTL_SETPOS    0x11

struct dev_s;
struct event_s;

/*
 * List convention: next is always the FIRST member of a list-able
 * structure, so the head variable and a member's next field are uniform
 * cells and all lists are handled by the same code (list.s).
 */

/*
 * Device driver descriptor — one per hardware type.
 * probe() ONLY enumerates: it builds the chain of the driver's STATICALLY
 * declared dev_t instances for the units present (presence comes from the
 * NVRAM/model config, not from poking hardware). Nothing is allocated and no
 * hardware is initialized. The chain is hooked into the global device list
 * with list_append().
 * init() is the driver-level (controller/chip) one-time setup, run once for
 * every driver when the kernel starts. It is the same for every unit of the
 * driver; per-device setup belongs in open(). Many drivers need none.
 * open() performs the per-DEVICE-instance init (which can differ unit to
 * unit); 0 = ok.
 *
 * read()/write() are ASYNCHRONOUS: the caller passes a sync event, the
 * driver starts the transfer (DMA + interrupt where possible) and signals
 * the event when the work is done. The caller waits on the event (or, for
 * immediate-completion drivers, finds it already signaled). A NULL event is
 * allowed (fire and forget). The asm native ABI is in drivers/drv.inc
 * (hl=dev, de=buf, bc=count, ix=event).
 *
 * All function pointers are mandatory; stub with an empty return for
 * operations the hardware does not support.
 */
typedef struct dev_drv_s {
    struct dev_drv_s *next;
    struct dev_s *(*probe) (void);
    int16_t       (*init)  (void);
    int16_t       (*open)  (struct dev_s *dev);
    void          (*close) (struct dev_s *dev);
    int16_t       (*read)  (struct dev_s *dev, uint8_t *buf, uint16_t len, struct event_s *event);
    int16_t       (*write) (struct dev_s *dev, const uint8_t *buf, uint16_t len, struct event_s *event);
    int16_t       (*ioctl) (struct dev_s *dev, uint8_t cmd, void *params);
} dev_drv_t;

/*
 * Device instance — one per unit discovered by probe.
 * next links instances into the global device list built at boot (first
 * member, see list convention above).
 * name is zero-terminated in 6 bytes (5 usable chars + null), which fits all
 * current built-in device names (`ctc`, `pioA`, `ttyS0`, `nvram`, `sda`).
 * data is 9 bytes of driver-private per-instance state; block devices
 * keep their current position here (reads/writes all share the same
 * parameters, the position advances with each transfer).
 */
typedef struct dev_s {
    struct dev_s *next;
    uint8_t     name[6];
    uint8_t     flags;
    uint8_t     data[9];
    dev_drv_t  *driver;
} dev_t;

/*
 * Head of the driver chain (every built-in driver descriptor, linked through
 * its next field) and head of the device chain (all units enumerated from the
 * NVRAM/model configuration). Both are built at kernel start.
 */
extern dev_drv_t *drv_first;
extern dev_t     *dev_first;

/*
 * Internal kernel bootstrap helpers for the built-in driver set.
 */
void __drv_register_all(void);

/*
 * Runs every driver's one-time, driver-level init (controller/chip setup)
 * once at kernel start. Call before the device probe pass.
 */
void __dev_init_all(void);

/*
 * Enumerate the configured devices into dev_first (NVRAM/model driven).
 */
void __dev_probe_all(void);

/*
 * Finds a device by name in the single global device list (built from
 * probe results at boot) and returns its driver via the dev_s back
 * pointer, or NULL if no device matches. For assembly callers the
 * matched dev_t itself is returned as well (in de), ready to be passed
 * to the driver's open().
 */
dev_drv_t *__find_dev_drv(const char *name);

#endif /* DEV_H */

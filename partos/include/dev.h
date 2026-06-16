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

struct dev_s;

/*
 * List convention: next is always the FIRST member of a list-able
 * structure, so the head variable and a member's next field are uniform
 * cells and all lists are handled by the same code (list.s).
 */

/*
 * Device driver descriptor — one per hardware type.
 * probe() detects hardware and returns a chain of the driver's own
 * STATICALLY declared dev_t instances (linked in place through their
 * next fields, zero-terminated), or NULL if no hardware is present.
 * Nothing is allocated or copied: the instances live in the BIOS image,
 * which is RAM at runtime, so probe only sets their next links to chain
 * the units actually found. The chain is then hooked into the global
 * device list with list_append().
 * open() receives an already-resolved device instance (see find_dev_drv)
 * and performs the hardware-specific claim/init; 0 = ok, negative = error.
 * All function pointers are mandatory; stub with an empty return for
 * operations the hardware does not support.
 */
typedef struct dev_drv_s {
    struct dev_drv_s *next;
    struct dev_s *(*probe) (void);
    int16_t       (*open)  (struct dev_s *dev);
    void          (*close) (struct dev_s *dev);
    int16_t       (*read)  (struct dev_s *dev, uint8_t *buf, uint16_t len);
    int16_t       (*write) (struct dev_s *dev, const uint8_t *buf, uint16_t len);
    int16_t       (*ioctl) (struct dev_s *dev, uint8_t cmd, void *params);
} dev_drv_t;

/*
 * Device instance — one per unit discovered by probe.
 * next links instances into the global device list built at boot (first
 * member, see list convention above).
 * name is zero-terminated in 9 bytes (8 usable chars + null).
 * data is 16 bytes of driver-private per-instance state; block devices
 * keep their current position here (reads/writes all share the same
 * parameters, the position advances with each transfer).
 */
typedef struct dev_s {
    struct dev_s *next;
    uint8_t     name[8];
    uint8_t     reserved;
    uint8_t     flags;
    uint8_t     data[16];
    dev_drv_t  *driver;
} dev_t;

/*
 * Finds a device by name in the single global device list (built from
 * probe results at boot) and returns its driver via the dev_s back
 * pointer, or NULL if no device matches. For assembly callers the
 * matched dev_t itself is returned as well (in de), ready to be passed
 * to the driver's open().
 */
dev_drv_t *find_dev_drv(const char *name);

#endif /* DEV_H */

/*
 * Documents the standard PartOS non-volatile RAM payload and the current
 * Partner firmware setup-byte layout stored in the RTC-backed NVRAM area.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef NVRAM_H
#define NVRAM_H

#include <stdint.h>

/*
 * Standard PartOS NVRAM payload used by the nvram device's read()/write()
 * calls. The driver preserves these 8 bytes exactly as stored by the
 * machine-specific firmware/setup code.
 */
#define NVRAM_BLOCK_SIZE            8

typedef struct nvram_block_s {
    uint8_t data[NVRAM_BLOCK_SIZE];
} nvram_block_t;

/*
 * Current Partner BIOS setup layout inside the raw 8-byte NVRAM block.
 * This is documentation-first: use masks/shifts rather than C bitfields so
 * the layout stays explicit and compiler-independent.
 *
 * Byte 0:
 *   bits 7:4  setup checksum nibble
 *   bits 3:0  boot device selector
 *
 * Byte 1:
 *   bits 7:6  fd0 type index
 *   bits 5:4  fd1 type index
 *   bits 3:2  fd2 type index
 *   bits 1:0  fd3 type index
 *
 * Byte 2:
 *   bits 7:6  sda type index
 *   bits 5:4  sdb type index
 *   bits 3:2  lp0 attachment kind
 *   bits 1:0  lp1 attachment kind
 *
 * Byte 3:
 *   bits 7:6  ttys0 attached device kind
 *   bits 5:4  ttys1 attached device kind
 *   bits 3:2  ttys2 attached device kind
 *   bits 1:0  ttys3 attached device kind
 *
 * Byte 4:
 *   ttys0 line format/config
 *     bits 7:5  speed code
 *     bit 4     stop bits
 *     bits 3:2  parity
 *     bit 1     data bits
 *     bit 0     reserved
 *
 * Byte 5:
 *   ttys1 line format/config
 *     bits 7:5  speed code
 *     bit 4     stop bits
 *     bits 3:2  parity
 *     bit 1     data bits
 *     bit 0     reserved
 *
 * Byte 6:
 *   ttys2 line format/config
 *     bits 7:5  speed code
 *     bit 4     stop bits
 *     bits 3:2  parity
 *     bit 1     data bits
 *     bit 0     reserved
 *
 * Byte 7:
 *   ttys3 line format/config
 *     bits 7:5  speed code
 *     bit 4     stop bits
 *     bits 3:2  parity
 *     bit 1     data bits
 *     bit 0     reserved
 *
 * BIOS should detect hardware first, then validate this block. If the
 * checksum is invalid, the setup must be treated as invalid and reset to
 * factory settings.
 */
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

#define PARTNER_BIOS_NVRAM_SIZE     NVRAM_BLOCK_SIZE

#define PARTNER_SETUP_CSUM_MASK     0xF0
#define PARTNER_SETUP_CSUM_SHIFT    4

#define PARTNER_BOOT_DEV_MASK       0x0F
#define PARTNER_BOOT_DEV_SHIFT      0

#define PARTNER_FD0_TYPE_MASK       0xC0
#define PARTNER_FD0_TYPE_SHIFT      6

#define PARTNER_FD1_TYPE_MASK       0x30
#define PARTNER_FD1_TYPE_SHIFT      4

#define PARTNER_FD2_TYPE_MASK       0x0C
#define PARTNER_FD2_TYPE_SHIFT      2

#define PARTNER_FD3_TYPE_MASK       0x03
#define PARTNER_FD3_TYPE_SHIFT      0

#define PARTNER_SDA_TYPE_MASK       0xC0
#define PARTNER_SDA_TYPE_SHIFT      6

#define PARTNER_SDB_TYPE_MASK       0x30
#define PARTNER_SDB_TYPE_SHIFT      4

#define PARTNER_LP0_KIND_MASK       0x0C
#define PARTNER_LP0_KIND_SHIFT      2

#define PARTNER_LP1_KIND_MASK       0x03
#define PARTNER_LP1_KIND_SHIFT      0

#define PARTNER_BOOT_SDA            0x0
#define PARTNER_BOOT_SDB            0x1
#define PARTNER_BOOT_FD0            0x2
#define PARTNER_BOOT_FD1            0x3
#define PARTNER_BOOT_FD2            0x4
#define PARTNER_BOOT_FD3            0x5

#define PARTNER_LP_KIND_NONE        0x0
#define PARTNER_LP_KIND_PRINTER     0x1
#define PARTNER_LP_KIND_COVOX       0x2
#define PARTNER_LP_KIND_FREE        0x3

#define PARTNER_TTYS_SPEED_MASK     0xE0
#define PARTNER_TTYS_SPEED_SHIFT    5
#define PARTNER_TTYS_SPEED_300      0x0
#define PARTNER_TTYS_SPEED_600      0x1
#define PARTNER_TTYS_SPEED_1200     0x2
#define PARTNER_TTYS_SPEED_2400     0x3
#define PARTNER_TTYS_SPEED_4800     0x4
#define PARTNER_TTYS_SPEED_9600     0x5
#define PARTNER_TTYS_SPEED_19200    0x6
#define PARTNER_TTYS_SPEED_FREE     0x7

#define PARTNER_TTYS_STOP_MASK      0x10
#define PARTNER_TTYS_STOP_SHIFT     4
#define PARTNER_TTYS_STOP_1         0x0
#define PARTNER_TTYS_STOP_2         0x1

#define PARTNER_TTYS_PARITY_MASK    0x0C
#define PARTNER_TTYS_PARITY_SHIFT   2
#define PARTNER_TTYS_PARITY_NONE    0x0
#define PARTNER_TTYS_PARITY_ODD     0x1
#define PARTNER_TTYS_PARITY_EVEN    0x2

#define PARTNER_TTYS_BITS_MASK      0x02
#define PARTNER_TTYS_BITS_SHIFT     1
#define PARTNER_TTYS_BITS_7         0x0
#define PARTNER_TTYS_BITS_8         0x1

/* byte 3, bits 7:6 */
#define PARTNER_TTYS0_ATTACH_MASK   0xC0
#define PARTNER_TTYS0_ATTACH_SHIFT  6

/* byte 3, bits 5:4 */
#define PARTNER_TTYS1_ATTACH_MASK   0x30
#define PARTNER_TTYS1_ATTACH_SHIFT  4

/* byte 3, bits 3:2 */
#define PARTNER_TTYS2_ATTACH_MASK   0x0C
#define PARTNER_TTYS2_ATTACH_SHIFT  2

/* byte 3, bits 1:0 */
#define PARTNER_TTYS3_ATTACH_MASK   0x03
#define PARTNER_TTYS3_ATTACH_SHIFT  0

#define PARTNER_TTYS_ATTACH_STDIN       0x0
#define PARTNER_TTYS_ATTACH_STDOUT      0x1
#define PARTNER_TTYS_ATTACH_MOUSE       0x2
#define PARTNER_TTYS_ATTACH_FREE        0x3

#endif /* NVRAM_H */

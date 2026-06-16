#!/usr/bin/env python3
# mkdosdisk.py
#
# Build bootable FAT12/FAT16 "superfloppy" images for the Iskra Delta Partner
# with 256-byte logical sectors (the geometry the emulator and the real
# i8272 / Xebec S1410 hardware use). Off-the-shelf tools (mkfs.fat, mtools)
# refuse sector sizes below 512, so the BPB / FAT / root directory are laid
# out by hand here.
#
# Layout (both images, no MBR partition table -- BPB lives at LBA 0):
#   sector 0          : boot sector (jump + BPB + 0x55AA signature at 254..255)
#   sectors 1..N      : reserved OS staging area (8 KiB, zeroed for the OS)
#   reserved region   : boot sector + OS staging  (BPB.reserved_sectors)
#   FAT region        : num_fats copies
#   root directory    : fixed-size FAT12/16 root
#   data region       : clusters
#
# 2026-06-15   tstih
import math
import struct
import sys

SECTOR = 256                      # logical sector size (Partner hardware)
OS_STAGING_BYTES = 8 * 1024       # 8 KiB reserved for the OS, after the boot sector
OS_STAGING_SECTORS = OS_STAGING_BYTES // SECTOR
RESERVED_SECTORS = 1 + OS_STAGING_SECTORS   # boot sector + OS staging


def fat_sectors(total, reserved, num_fats, root_sectors, spc, fat_bits):
    """Smallest sectors-per-FAT that can hold its own cluster count."""
    entry = 1.5 if fat_bits == 12 else 2.0
    fatsz = 0
    while True:
        fatsz += 1
        data = total - reserved - num_fats * fatsz - root_sectors
        clusters = data // spc
        need = math.ceil(((clusters + 2) * entry) / SECTOR)
        if fatsz >= need:
            return fatsz, clusters


def build_bpb(total_sectors, spc, root_entries, media, spt, heads,
              fatsz, drive_num, label, fstype):
    bs = bytearray(SECTOR)
    # jump + nop (standard FAT jump field; not executed on the Z80 Partner,
    # present only so the image is a well-formed FAT volume)
    bs[0:3] = bytes((0xEB, 0x3C, 0x90))
    bs[3:11] = b"PARTOS  "                      # OEM name
    struct.pack_into("<H", bs, 0x0B, SECTOR)    # bytes per sector
    bs[0x0D] = spc                              # sectors per cluster
    struct.pack_into("<H", bs, 0x0E, RESERVED_SECTORS)
    bs[0x10] = 2                                # number of FATs
    struct.pack_into("<H", bs, 0x11, root_entries)
    if total_sectors < 0x10000:
        struct.pack_into("<H", bs, 0x13, total_sectors)
        struct.pack_into("<I", bs, 0x20, 0)
    else:
        struct.pack_into("<H", bs, 0x13, 0)
        struct.pack_into("<I", bs, 0x20, total_sectors)
    bs[0x15] = media
    struct.pack_into("<H", bs, 0x16, fatsz)     # sectors per FAT
    struct.pack_into("<H", bs, 0x18, spt)       # sectors per track
    struct.pack_into("<H", bs, 0x1A, heads)
    struct.pack_into("<I", bs, 0x1C, 0)         # hidden sectors
    bs[0x24] = drive_num
    bs[0x26] = 0x29                             # extended boot signature
    struct.pack_into("<I", bs, 0x27, 0x50415254)  # volume id ("PART")
    bs[0x2B:0x36] = label.ljust(11)[:11].encode("ascii")
    bs[0x36:0x3E] = fstype.ljust(8)[:8].encode("ascii")
    # boot signature at the end of the 256-byte sector
    bs[SECTOR - 2] = 0x55
    bs[SECTOR - 1] = 0xAA
    return bs


def build_image(path, total_sectors, spc, root_entries, media, spt, heads,
                fat_bits, drive_num, label):
    root_sectors = math.ceil(root_entries * 32 / SECTOR)
    fatsz, clusters = fat_sectors(total_sectors, RESERVED_SECTORS, 2,
                                  root_sectors, spc, fat_bits)
    fstype = "FAT12   " if fat_bits == 12 else "FAT16   "

    img = bytearray(total_sectors * SECTOR)

    # boot sector + BPB
    bs = build_bpb(total_sectors, spc, root_entries, media, spt, heads,
                   fatsz, drive_num, label, fstype)
    img[0:SECTOR] = bs

    # FATs: cluster 0 = media + 0xFF.., cluster 1 = EOC
    fat = bytearray(fatsz * SECTOR)
    if fat_bits == 12:
        fat[0] = media
        fat[1] = 0xFF
        fat[2] = 0xFF
    else:
        fat[0] = media
        fat[1] = 0xFF
        fat[2] = 0xFF
        fat[3] = 0xFF
    fat_start = RESERVED_SECTORS
    for i in range(2):
        off = (fat_start + i * fatsz) * SECTOR
        img[off:off + len(fat)] = fat

    # root directory: a single volume-label entry
    root_start = fat_start + 2 * fatsz
    vol = bytearray(32)
    vol[0:11] = label.ljust(11)[:11].encode("ascii")
    vol[11] = 0x08                                  # ATTR_VOLUME_ID
    img[root_start * SECTOR: root_start * SECTOR + 32] = vol

    with open(path, "wb") as f:
        f.write(img)

    data_start = root_start + root_sectors
    print(f"{path}: {fstype.strip()}  {total_sectors} x {SECTOR}B "
          f"= {len(img)} bytes")
    print(f"    reserved={RESERVED_SECTORS} (boot + {OS_STAGING_SECTORS} OS "
          f"= {OS_STAGING_BYTES} B), fat_sectors={fatsz} x2, "
          f"root_sectors={root_sectors}, data@{data_start}, clusters={clusters}")


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "disks"

    # Floppy: 720 KiB, 80 cyl x 2 heads x 18 spt x 256 B = 737,280 bytes.
    # Matches the emulator's 80-track double-sided detection (load_disk()).
    build_image(f"{out_dir}/fdd-dos.img",
                total_sectors=80 * 2 * 18, spc=1, root_entries=112,
                media=0xF9, spt=18, heads=2, fat_bits=12,
                drive_num=0x00, label="FDD DOS    ")

    # Hard disk: Seagate ST-412, 306 cyl x 4 heads x 32 spt x 256 B
    # = 10,027,008 bytes. The Xebec presents pure LBA blocks of 256 B.
    build_image(f"{out_dir}/hdd-dos.img",
                total_sectors=306 * 4 * 32, spc=2, root_entries=512,
                media=0xF8, spt=32, heads=4, fat_bits=16,
                drive_num=0x80, label="HDD DOS    ")


if __name__ == "__main__":
    main()

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
#   sector 0          : boot sector (jump + BPB + optional 0x55AA at 254..255)
#   sectors 1..8      : reserved micro-kernel image  -> 0x0000
#   sectors 9..72     : reserved OS payload image    -> 0xC000
#   reserved region   : boot sector + split OS images (BPB.reserved_sectors)
#   FAT region        : num_fats copies
#   root directory    : fixed-size FAT12/16 root
#   data region       : clusters
#
# 2026-06-15   tstih
import math
import os
import re
import struct
import sys

SECTOR = 256                      # logical sector size (Partner hardware)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
PARTOS_INC = os.path.join(ROOT_DIR, "partos", "src", "partos.inc")
DEFAULT_UKERNEL = os.path.join(ROOT_DIR, "partos", "bin", "kernel.sys")
DEFAULT_SERVICES = os.path.join(ROOT_DIR, "partos", "bin", "os.sys")
DEFAULT_SHELL = os.path.join(ROOT_DIR, "partos", "bin", "shell.com")
DEFAULT_LS = os.path.join(ROOT_DIR, "partos", "bin", "ls.com")
DEFAULT_PS = os.path.join(ROOT_DIR, "partos", "bin", "ps.com")
DEFAULT_MEM = os.path.join(ROOT_DIR, "partos", "bin", "mem.com")
DEFAULT_CAT = os.path.join(ROOT_DIR, "partos", "bin", "cat.com")
DEFAULT_MKDIR = os.path.join(ROOT_DIR, "partos", "bin", "mkdir.com")
DEFAULT_RMDIR = os.path.join(ROOT_DIR, "partos", "bin", "rmdir.com")
DEFAULT_DEL = os.path.join(ROOT_DIR, "partos", "bin", "del.com")
DEFAULT_CP = os.path.join(ROOT_DIR, "partos", "bin", "cp.com")
DEFAULT_MV = os.path.join(ROOT_DIR, "partos", "bin", "mv.com")
DEFAULT_CLEAR = os.path.join(ROOT_DIR, "partos", "bin", "clear.com")
DEFAULT_ECHO = os.path.join(ROOT_DIR, "partos", "bin", "echo.com")
DEFAULT_HELP = os.path.join(ROOT_DIR, "partos", "bin", "help.com")
DEFAULT_TOUCH = os.path.join(ROOT_DIR, "partos", "bin", "touch.com")
DEFAULT_RM = os.path.join(ROOT_DIR, "partos", "bin", "rm.com")

# Legacy 8 KiB staging window used by --shelldisk (direct-kernel fixture).
LEGACY_OS_STAGING_SECTORS = 32

# Split load map (must match partos.inc): the ROM loads two reserved regions.
#   lba 1..8   2 KB  -> 0x0000  micro-kernel (mirrored into both banks)
#   lba 9..72  16 KB -> 0xc000  shared services + os data
UKERNEL_SECTORS = 8               # 2 KB
SERVICES_SECTORS = 64             # 16 KB
SPLIT_RESERVED = 1 + UKERNEL_SECTORS + SERVICES_SECTORS   # boot + 2 KB + 16 KB


def read_inc_equ(name, path=PARTOS_INC):
    pat = re.compile(rf"\.equ\s+{re.escape(name)},\s+(0x[0-9A-Fa-f]+)")
    with open(path, encoding="ascii") as f:
        for line in f:
            m = pat.search(line)
            if m:
                return int(m.group(1), 16)
    raise RuntimeError(f"{name} not found in {path}")


def read_image(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        raise RuntimeError(f"empty image: {path}")
    return data


def pack_split(img, ukernel_bytes, services_bytes):
    """Lay out the split reserved region the ROM loader expects:
         lba 1..8   (2 KB)  <- ukernel_bytes   -> 0x0000
         lba 9..72  (16 KB) <- services_bytes  -> 0xc000
    Each region must fit its sector window; unused tail sectors stay zero."""
    if len(ukernel_bytes) > UKERNEL_SECTORS * SECTOR:
        raise RuntimeError(
            f"micro-kernel {len(ukernel_bytes)} B exceeds "
            f"{UKERNEL_SECTORS * SECTOR} B ({UKERNEL_SECTORS} sectors)")
    if len(services_bytes) > SERVICES_SECTORS * SECTOR:
        raise RuntimeError(
            f"services {len(services_bytes)} B exceeds "
            f"{SERVICES_SECTORS * SECTOR} B ({SERVICES_SECTORS} sectors)")
    low = 1 * SECTOR
    img[low:low + len(ukernel_bytes)] = ukernel_bytes
    high = (1 + UKERNEL_SECTORS) * SECTOR
    img[high:high + len(services_bytes)] = services_bytes
    return UKERNEL_SECTORS + SERVICES_SECTORS

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
              fatsz, drive_num, label, fstype, reserved_sectors, bootable):
    bs = bytearray(SECTOR)
    bs[0:3] = bytes((0xEB, 0x3C, 0x90))
    bs[3:11] = b"PARTOS  "
    struct.pack_into("<H", bs, 0x0B, SECTOR)
    bs[0x0D] = spc
    struct.pack_into("<H", bs, 0x0E, reserved_sectors)
    bs[0x10] = 2
    struct.pack_into("<H", bs, 0x11, root_entries)
    if total_sectors < 0x10000:
        struct.pack_into("<H", bs, 0x13, total_sectors)
        struct.pack_into("<I", bs, 0x20, 0)
    else:
        struct.pack_into("<H", bs, 0x13, 0)
        struct.pack_into("<I", bs, 0x20, total_sectors)
    bs[0x15] = media
    struct.pack_into("<H", bs, 0x16, fatsz)
    struct.pack_into("<H", bs, 0x18, spt)
    struct.pack_into("<H", bs, 0x1A, heads)
    struct.pack_into("<I", bs, 0x1C, 0)
    bs[0x24] = drive_num
    bs[0x26] = 0x29
    struct.pack_into("<I", bs, 0x27, 0x50415254)
    bs[0x2B:0x36] = label.ljust(11)[:11].encode("ascii")
    bs[0x36:0x3E] = fstype.ljust(8)[:8].encode("ascii")
    if bootable:
        bs[SECTOR - 2] = 0x55
        bs[SECTOR - 1] = 0xAA
    return bs


def _set_fat(fat, fat_bits, cluster, value):
    if fat_bits == 12:
        off = cluster + (cluster >> 1)
        if cluster & 1:
            fat[off] = (fat[off] & 0x0F) | ((value << 4) & 0xF0)
            fat[off + 1] = (value >> 4) & 0xFF
        else:
            fat[off] = value & 0xFF
            fat[off + 1] = (fat[off + 1] & 0xF0) | ((value >> 8) & 0x0F)
    else:
        struct.pack_into("<H", fat, cluster * 2, value & 0xFFFF)


def build_image(path, total_sectors, spc, root_entries, media, spt, heads,
                fat_bits, drive_num, label, reserved_sectors,
                files=None, split_bytes=None, bootable=True):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    root_sectors = math.ceil(root_entries * 32 / SECTOR)
    fatsz, clusters = fat_sectors(total_sectors, reserved_sectors, 2,
                                  root_sectors, spc, fat_bits)
    fstype = "FAT12   " if fat_bits == 12 else "FAT16   "

    img = bytearray(total_sectors * SECTOR)

    bs = build_bpb(total_sectors, spc, root_entries, media, spt, heads,
                   fatsz, drive_num, label, fstype, reserved_sectors, bootable)
    img[0:SECTOR] = bs

    os_staging_sectors = reserved_sectors - 1
    if split_bytes is not None:
        packed = pack_split(img, split_bytes[0], split_bytes[1])
        if packed != os_staging_sectors:
            raise RuntimeError(
                f"split pack mismatch for {path}: BPB reserves {os_staging_sectors} "
                f"OS sectors but split layout needs {packed}")

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
    fat_start = reserved_sectors

    root_start = fat_start + 2 * fatsz
    vol = bytearray(32)
    vol[0:11] = label.ljust(11)[:11].encode("ascii")
    vol[11] = 0x08
    img[root_start * SECTOR: root_start * SECTOR + 32] = vol

    next_cluster = 2
    slot = 1
    cluster_bytes = spc * SECTOR
    for name11, data in (files or []):
        nclusters = max(1, math.ceil(len(data) / cluster_bytes))
        start_cluster = next_cluster
        for i in range(nclusters):
            cl = start_cluster + i
            sec = (root_start + root_sectors) + (cl - 2) * spc
            chunk = data[i * cluster_bytes:(i + 1) * cluster_bytes]
            img[sec * SECTOR: sec * SECTOR + len(chunk)] = chunk
            eoc = 0xFFF if fat_bits == 12 else 0xFFFF
            _set_fat(fat, fat_bits, cl, eoc if i == nclusters - 1 else cl + 1)
        ent = bytearray(32)
        ent[0:11] = name11.ljust(11)[:11].encode("ascii")
        ent[11] = 0x20
        struct.pack_into("<H", ent, 26, start_cluster)
        struct.pack_into("<I", ent, 28, len(data))
        eoff = root_start * SECTOR + slot * 32
        img[eoff:eoff + 32] = ent
        slot += 1
        next_cluster += nclusters

    for i in range(2):
        off = (fat_start + i * fatsz) * SECTOR
        img[off:off + len(fat)] = fat

    with open(path, "wb") as f:
        f.write(img)

    data_start = root_start + root_sectors
    os_bytes = os_staging_sectors * SECTOR
    print(f"{path}: {fstype.strip()}  {total_sectors} x {SECTOR}B = {len(img)} bytes")
    print(f"    reserved={reserved_sectors} (boot + {os_staging_sectors} OS = {os_bytes} B), "
          f"bootable={bootable}, fat_sectors={fatsz} x2, "
          f"root_sectors={root_sectors}, data@{data_start}, clusters={clusters}")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--shelldisk":
        path = sys.argv[2] if len(sys.argv) > 2 else "/tmp/fat-shell.img"
        reserved = 1 + LEGACY_OS_STAGING_SECTORS
        shell_path = os.environ.get("PARTOS_SHELL", DEFAULT_SHELL)
        ls_path = os.environ.get("PARTOS_LS", DEFAULT_LS)
        ps_path = os.environ.get("PARTOS_PS", DEFAULT_PS)
        mem_path = os.environ.get("PARTOS_MEM", DEFAULT_MEM)
        cat_path = os.environ.get("PARTOS_CAT", DEFAULT_CAT)
        mkdir_path = os.environ.get("PARTOS_MKDIR", DEFAULT_MKDIR)
        rmdir_path = os.environ.get("PARTOS_RMDIR", DEFAULT_RMDIR)
        del_path = os.environ.get("PARTOS_DEL", DEFAULT_DEL)
        cp_path = os.environ.get("PARTOS_CP", DEFAULT_CP)
        mv_path = os.environ.get("PARTOS_MV", DEFAULT_MV)
        clear_path = os.environ.get("PARTOS_CLEAR", DEFAULT_CLEAR)
        echo_path = os.environ.get("PARTOS_ECHO", DEFAULT_ECHO)
        help_path = os.environ.get("PARTOS_HELP", DEFAULT_HELP)
        touch_path = os.environ.get("PARTOS_TOUCH", DEFAULT_TOUCH)
        rm_path = os.environ.get("PARTOS_RM", DEFAULT_RM)
        if not os.path.isfile(shell_path):
            print(f"error: shell image not found at {shell_path} (build partos first)", file=sys.stderr)
            sys.exit(1)
        if not os.path.isfile(ls_path):
            print(f"error: ls image not found at {ls_path} (build partos first)", file=sys.stderr)
            sys.exit(1)
        if not os.path.isfile(ps_path):
            print(f"error: ps image not found at {ps_path} (build partos first)", file=sys.stderr)
            sys.exit(1)
        for label, path in (
            ("mem", mem_path),
            ("cat", cat_path),
            ("mkdir", mkdir_path),
            ("rmdir", rmdir_path),
            ("del", del_path),
            ("cp", cp_path),
            ("mv", mv_path),
            ("clear", clear_path),
            ("echo", echo_path),
            ("help", help_path),
            ("touch", touch_path),
            ("rm", rm_path),
        ):
            if not os.path.isfile(path):
                print(f"error: {label} image not found at {path} (build partos first)", file=sys.stderr)
                sys.exit(1)
        shell = read_image(shell_path)
        ls_cmd = read_image(ls_path)
        ps_cmd = read_image(ps_path)
        mem_cmd = read_image(mem_path)
        cat_cmd = read_image(cat_path)
        mkdir_cmd = read_image(mkdir_path)
        rmdir_cmd = read_image(rmdir_path)
        del_cmd = read_image(del_path)
        cp_cmd = read_image(cp_path)
        mv_cmd = read_image(mv_path)
        clear_cmd = read_image(clear_path)
        echo_cmd = read_image(echo_path)
        help_cmd = read_image(help_path)
        touch_cmd = read_image(touch_path)
        rm_cmd = read_image(rm_path)
        build_image(path, total_sectors=80 * 2 * 18, spc=1, root_entries=112,
                    media=0xF9, spt=18, heads=2, fat_bits=12,
                    drive_num=0x00, label="FDD DOS    ",
                    reserved_sectors=reserved,
                    files=[("SHELL   COM", shell), ("LS      COM", ls_cmd), ("PS      COM", ps_cmd),
                           ("MEM     COM", mem_cmd), ("CAT     COM", cat_cmd),
                           ("MKDIR   COM", mkdir_cmd), ("RMDIR   COM", rmdir_cmd),
                           ("DEL     COM", del_cmd), ("CP      COM", cp_cmd),
                           ("MV      COM", mv_cmd), ("CLEAR   COM", clear_cmd),
                           ("ECHO    COM", echo_cmd), ("HELP    COM", help_cmd),
                           ("TOUCH   COM", touch_cmd), ("RM      COM", rm_cmd)],
                    bootable=True)
        print(f"{path}: FAT12 floppy with shell/tools payload set")
        return

    ukernel_path = os.environ.get("PARTOS_UKERNEL", DEFAULT_UKERNEL)
    services_path = os.environ.get("PARTOS_SERVICES", DEFAULT_SERVICES)
    shell_path = os.environ.get("PARTOS_SHELL", DEFAULT_SHELL)
    ls_path = os.environ.get("PARTOS_LS", DEFAULT_LS)
    ps_path = os.environ.get("PARTOS_PS", DEFAULT_PS)
    mem_path = os.environ.get("PARTOS_MEM", DEFAULT_MEM)
    cat_path = os.environ.get("PARTOS_CAT", DEFAULT_CAT)
    mkdir_path = os.environ.get("PARTOS_MKDIR", DEFAULT_MKDIR)
    rmdir_path = os.environ.get("PARTOS_RMDIR", DEFAULT_RMDIR)
    del_path = os.environ.get("PARTOS_DEL", DEFAULT_DEL)
    cp_path = os.environ.get("PARTOS_CP", DEFAULT_CP)
    mv_path = os.environ.get("PARTOS_MV", DEFAULT_MV)
    clear_path = os.environ.get("PARTOS_CLEAR", DEFAULT_CLEAR)
    echo_path = os.environ.get("PARTOS_ECHO", DEFAULT_ECHO)
    help_path = os.environ.get("PARTOS_HELP", DEFAULT_HELP)
    touch_path = os.environ.get("PARTOS_TOUCH", DEFAULT_TOUCH)
    rm_path = os.environ.get("PARTOS_RM", DEFAULT_RM)
    if not os.path.isfile(ukernel_path):
        print(f"error: micro-kernel not found at {ukernel_path} (build partos first)", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(services_path):
        print(f"error: services image not found at {services_path} (build partos first)", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(shell_path):
        print(f"error: shell image not found at {shell_path} (build partos first)", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(ls_path):
        print(f"error: ls image not found at {ls_path} (build partos first)", file=sys.stderr)
        sys.exit(1)
    if not os.path.isfile(ps_path):
        print(f"error: ps image not found at {ps_path} (build partos first)", file=sys.stderr)
        sys.exit(1)
    for label, path in (
        ("mem", mem_path),
        ("cat", cat_path),
        ("mkdir", mkdir_path),
        ("rmdir", rmdir_path),
        ("del", del_path),
        ("cp", cp_path),
        ("mv", mv_path),
        ("clear", clear_path),
        ("echo", echo_path),
        ("help", help_path),
        ("touch", touch_path),
        ("rm", rm_path),
    ):
        if not os.path.isfile(path):
            print(f"error: {label} image not found at {path} (build partos first)", file=sys.stderr)
            sys.exit(1)

    ukernel_bytes = read_image(ukernel_path)
    services_bytes = read_image(services_path)
    shell = read_image(shell_path)
    ls_cmd = read_image(ls_path)
    ps_cmd = read_image(ps_path)
    mem_cmd = read_image(mem_path)
    cat_cmd = read_image(cat_path)
    mkdir_cmd = read_image(mkdir_path)
    rmdir_cmd = read_image(rmdir_path)
    del_cmd = read_image(del_path)
    cp_cmd = read_image(cp_path)
    mv_cmd = read_image(mv_path)
    clear_cmd = read_image(clear_path)
    echo_cmd = read_image(echo_path)
    help_cmd = read_image(help_path)
    touch_cmd = read_image(touch_path)
    rm_cmd = read_image(rm_path)
    print(f"split images: ukernel {len(ukernel_bytes)} B "
          f"(lba 1..{UKERNEL_SECTORS}) + services {len(services_bytes)} B "
          f"(lba {1 + UKERNEL_SECTORS}..{UKERNEL_SECTORS + SERVICES_SECTORS}), "
          f"reserved_sectors={SPLIT_RESERVED}")

    out_dir = sys.argv[1] if len(sys.argv) > 1 else "disks"

    # Floppy: valid FAT volume for later kernel mounts, but NOT ROM-bootable
    # (no 0x55AA) and no /SHELL.COM so the bootstrap falls through to sda.
    build_image(f"{out_dir}/fdd-dos.img",
                total_sectors=80 * 2 * 18, spc=1, root_entries=112,
                media=0xF9, spt=18, heads=2, fat_bits=12,
                drive_num=0x00, label="FDD DOS    ",
                reserved_sectors=1 + LEGACY_OS_STAGING_SECTORS,
                files=None, bootable=False)

    # Hard disk: ROM-bootable, carries the split OS image (2 KB low + 16 KB
    # high) in the reserved region the new ROM loader streams to 0x0000/0xc000.
    build_image(f"{out_dir}/hdd-dos.img",
                total_sectors=306 * 4 * 32, spc=2, root_entries=512,
                media=0xF8, spt=32, heads=4, fat_bits=16,
                drive_num=0x80, label="HDD DOS    ",
                reserved_sectors=SPLIT_RESERVED,
                files=[("SHELL   COM", shell), ("LS      COM", ls_cmd), ("PS      COM", ps_cmd),
                       ("MEM     COM", mem_cmd), ("CAT     COM", cat_cmd),
                       ("MKDIR   COM", mkdir_cmd), ("RMDIR   COM", rmdir_cmd),
                       ("DEL     COM", del_cmd), ("CP      COM", cp_cmd),
                       ("MV      COM", mv_cmd), ("CLEAR   COM", clear_cmd),
                       ("ECHO    COM", echo_cmd), ("HELP    COM", help_cmd),
                       ("TOUCH   COM", touch_cmd), ("RM      COM", rm_cmd)],
                split_bytes=(ukernel_bytes, services_bytes), bootable=True)


if __name__ == "__main__":
    main()

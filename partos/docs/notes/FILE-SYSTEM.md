# Note: File System Thoughts

Category: OS / File System / Boot Media
Date(s): 2023-07-30, 2026-06-11 (merged from `2023-07-30_file-system.md`)

Some thoughts about the file system for PartOS (originally "idp-os").

1) The floppy controller 8272 should be able to control any disk, not just
   a 5.25" floppy disk. This means that in Partner, you could insert a
   3.5" floppy 1.44 MB, and (from the hardware perspective) it should work.

2) Regarding the format of the floppy disk, hypothetically, FAT12 and
   FAT16 would work. ROM reads a sufficiently large boot sector that
   includes 512 bytes and jumps to its beginning. And FAT12/16 has, as its
   first command, a jump to the BOOT sector, followed by the BPB (BIOS
   Parameter Block). ROM would not detect BPB, but the boot sector could
   easily do it. This means that it can boot from any DOS diskette.

3) The dilemma is whether to use FAT12 or FAT16. FAT12 allows up to 16 MB
   hard disks. Xebec S1410, on the other hand, supports MFM disks, which
   can go up to 40 MB.

4) Regarding startup, the system must configure the 8272 controller with
   disk parameters before first operation. And with FAT, it can read
   format details from the BPB (BIOS Parameter Block) on the disk.

~~~asm
    ;; 3 bytes for jump after the BPB
    jr boot_code

    OEMLabel:           db "mkfs.fat"
    SectorSize:         dw 512
    SectorsPerCluster:  db 1
    ReservedForBoot:    dw 1
    NumberOfFats:       db 2
    NumRootDirEntrys:   dw 224
    LogicalSectors:     dw 2880
    MediumByte:         db 0F0h
    SectorsPerFat:      dw 9
    SectorsPerTrack:    dw 18
    NumberOfHeads:      dw 2
    HiddenSectors:      dd 0
    LargeSectors:       dd 0
    DriveNumber:        dw 0
    Signature:          db 41
    VolumeID:           dd 0
    VolumeLabel:        db "FatTest    "
    FileSystem:         db "FAT12  "
~~~

5) So, we can have standard PC disks for the OS, and it can also be set on
   an emulator (Gotek), as long as we manage to fit the loader into
   512 bytes.

6) Links to decent FAT12/16 documentation:

- [A simple bootloader](https://blog.kalehmann.de/blog/2017/07/20/simple-boot-loader.html)
- [FAT16 File System Specification](http://www.maverick-os.dk/FileSystemFormats/FAT16_FileSystem.html)
- [CPM 2.2 with FAT](https://ciernioo.wordpress.com/2016/05/11/cpm-2-2-up-and-running/)
- [CPUville](http://cpuville.com/Code/Z80.html)
- [weirDOS CPM 2.2 w FAT](https://github.com/256byteram/WeirDOS)
- [CPM FAT](https://github.com/z80playground/cpm-fat)

## 2026-06-11 context

Note that the Partner's original media use **256-byte sectors** (FDD:
2x80x18x256; HDD: 1224x32x256 per the emulator image geometry), while the
FAT plan above assumes 512-byte sectors. Sector size on the 8272 is per-
command (N parameter), so reformatting to 512-byte sectors is plausible —
to be validated against drive/controller limits and the emulator.

            ;; fat_mutate.s
            ;;
            ;; FAT12/FAT16 metadata mutation helpers for PartOS.
            ;;
            ;; this module keeps the write-side logic small by centralizing:
            ;;   - FAT entry writes for both FAT12 and FAT16
            ;;   - free-cluster search with fs->alloc_hint wrapping
            ;;   - cluster-chain extension
            ;;   - zero-fill for newly added directory clusters
            ;;   - directory-entry metadata writeback for open files
            ;;
            ;; only the tiny 12-bit nibble packing differs between FAT12 and
            ;; FAT16. all higher-level create/grow logic reuses the same helper
            ;; flow from here.
            ;; ----------------------------------------------------------------
            ;; 2026-06-17   tstih
            .module fat_mutate

            .include "fat.inc"

            .globl  fat_set_fat_entry$
            .globl  fat_find_free_cluster$
            .globl  fat_alloc_chain_cluster$
            .globl  fat_zero_cluster$
            .globl  fat_update_file_dirent$

            .globl  fat_load_de$
            .globl  fat_fs_de$
            .globl  fat_fs_byte$
            .globl  fat_store_de$
            .globl  fat_read_volume_sector$
            .globl  fat_write_volume_sector$
            .globl  fat_cluster_valid$
            .globl  fat_cluster_to_sector$
            .globl  fat_fat_offset$
            .globl  fat_get_fat_entry$
            .globl  fat_work_fs$
            .globl  fat_work_dev$
            .globl  fat_fat_offset_tmp$
            .globl  fat_tmp_cluster$
            .globl  fat_offset_low$
            .globl  fat_pair$
            .globl  fat_sector$

            .area   _CODE

            ;; fat_write_fat_copies$(<de> rel_sector) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; writes the already prepared fat_sector$ buffer back to every FAT
            ;; copy at the same volume-relative sector number.
            ;; ----------------------------------------------------------------
fat_write_fat_copies$:
            ld      (fat_mut_sector$),de
            ld      bc,#FATFS_SECTORS_PER_FAT
            call    fat_fs_de$
            ld      (fat_mut_step$),de

            ld      bc,#FATFS_NUM_FATS
            call    fat_fs_byte$
            ld      (fat_mut_count$),a

fwfc_loop$:
            ld      de,(fat_mut_sector$)
            call    fat_write_volume_sector$
            ld      a,h
            or      l
            ret     nz

            ld      a,(fat_mut_count$)
            dec     a
            ld      (fat_mut_count$),a
            jr      z,fwfc_ok$

            ld      hl,(fat_mut_sector$)
            ld      de,(fat_mut_step$)
            add     hl,de
            ld      (fat_mut_sector$),hl
            jr      fwfc_loop$

fwfc_ok$:
            ld      hl,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_eoc_value$() -> <hl> eoc_marker
            ;; ----------------------------------------------------------------
fat_eoc_value$:
            ld      bc,#FATFS_FAT_BITS
            call    fat_fs_byte$
            cp      #12
            ld      hl,#0xffff
            ret     nz
            ld      hl,#0x0fff
            ret

            ;; ----------------------------------------------------------------
            ;; fat_advance_alloc_hint$(<de> cluster)
            ;; ----------------------------------------------------------------
            ;; keeps the next free-cluster scan moving forward while wrapping
            ;; back to cluster 2 once the current volume limit is crossed.
            ;; ----------------------------------------------------------------
fat_advance_alloc_hint$:
            ex      de,hl
            inc     hl
            ex      de,hl
            push    de
            call    fat_cluster_valid$
            pop     de
            jr      nz,fah_store$
            ld      de,#FAT_CLUSTER_FIRST
fah_store$:
            ld      hl,(fat_work_fs$)
            ld      bc,#FATFS_ALLOC_HINT
            add     hl,bc
            call    fat_store_de$
            ret

            ;; ----------------------------------------------------------------
            ;; fat_set_fat_entry$(<de> cluster, <hl> value) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; updates one FAT12 or FAT16 entry and mirrors the touched sector
            ;; to every FAT copy. only FAT12 even entries can straddle sectors.
            ;; ----------------------------------------------------------------
fat_set_fat_entry$:
            ld      (fat_tmp_cluster$),de
            ld      (fat_mut_value$),hl
            call    fat_fat_offset$
            ld      (fat_fat_offset_tmp$),de

            push    de
            ld      bc,#FATFS_FAT_START
            call    fat_fs_de$
            pop     hl
            ld      a,l
            ld      (fat_offset_low$),a
            ld      l,h
            ld      h,#0
            add     hl,de
            ex      de,hl
            ld      (fat_mut_sector$),de
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            ret     nz

            ld      a,(fat_offset_low$)
            ld      l,a
            ld      h,#0
            ld      de,#fat_sector$
            add     hl,de

            push    hl
            ld      bc,#FATFS_FAT_BITS
            call    fat_fs_byte$
            pop     hl
            cp      #12
            jp      nz,fsfe_16$

            ld      de,(fat_tmp_cluster$)
            bit     0,e
            jr      nz,fsfe_12_odd$

            ld      de,(fat_mut_value$)
            ld      a,e
            ld      (hl),a
            ld      a,(fat_offset_low$)
            cp      #0xff
            jr      z,fsfe_12_even_cross$
            inc     hl
            ld      a,(hl)
            and     #0xf0
            ld      b,a
            ld      a,d
            and     #0x0f
            or      b
            ld      (hl),a
            ld      de,(fat_mut_sector$)
            jp      fat_write_fat_copies$

fsfe_12_even_cross$:
            ld      de,(fat_mut_sector$)
            call    fat_write_fat_copies$
            ld      a,h
            or      l
            ret     nz

            ld      de,(fat_mut_sector$)
            inc     de
            ld      (fat_mut_sector$),de
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            ret     nz

            ld      a,(fat_sector$)
            and     #0xf0
            ld      b,a
            ld      de,(fat_mut_value$)
            ld      a,d
            and     #0x0f
            or      b
            ld      (fat_sector$),a
            ld      de,(fat_mut_sector$)
            jp      fat_write_fat_copies$

fsfe_12_odd$:
            ld      de,(fat_mut_value$)
            ld      a,e
            and     #0x0f
            add     a,a
            add     a,a
            add     a,a
            add     a,a
            ld      b,a
            ld      a,(hl)
            and     #0x0f
            or      b
            ld      (hl),a
            inc     hl
            ld      a,d
            add     a,a
            add     a,a
            add     a,a
            add     a,a
            and     #0xf0
            ld      b,a
            ld      a,e
            rrca
            rrca
            rrca
            rrca
            and     #0x0f
            or      b
            ld      (hl),a
            ld      de,(fat_mut_sector$)
            jp      fat_write_fat_copies$

fsfe_16$:
            ld      de,(fat_mut_value$)
            ld      a,e
            ld      (hl),a
            inc     hl
            ld      a,d
            ld      (hl),a
            ld      de,(fat_mut_sector$)
            jp      fat_write_fat_copies$

            ;; ----------------------------------------------------------------
            ;; fat_find_free_cluster$() -> <hl> rc, <de> cluster
            ;; ----------------------------------------------------------------
            ;; scans from fs->alloc_hint and wraps once until it finds a zero
            ;; FAT entry. the winning cluster number is returned in de.
            ;; ----------------------------------------------------------------
fat_find_free_cluster$:
            ld      bc,#FATFS_ALLOC_HINT
            call    fat_fs_de$
            push    de
            call    fat_cluster_valid$
            pop     de
            jr      nz,fffc_hint_ok$
            ld      de,#FAT_CLUSTER_FIRST
fffc_hint_ok$:
            ld      (fat_mut_start$),de
            ld      (fat_mut_curr$),de

fffc_loop$:
            ld      de,(fat_mut_curr$)
            call    fat_get_fat_entry$
            ld      a,h
            or      l
            ret     nz
            ld      a,d
            or      e
            jr      z,fffc_found$

            ld      hl,(fat_mut_curr$)
            inc     hl
            ex      de,hl
            push    de
            call    fat_cluster_valid$
            pop     de
            jr      nz,fffc_store_next$
            ld      de,#FAT_CLUSTER_FIRST
fffc_store_next$:
            ld      (fat_mut_curr$),de
            ld      hl,(fat_mut_start$)
            ld      a,e
            cp      l
            jr      nz,fffc_loop$
            ld      a,d
            cp      h
            jr      nz,fffc_loop$
            ld      hl,#FAT_ENOSPC
            ret

fffc_found$:
            ld      de,(fat_mut_curr$)
            ld      hl,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_alloc_chain_cluster$(<de> prev_cluster) -> <hl> rc, <de> new
            ;; ----------------------------------------------------------------
            ;; allocates one free cluster, marks it end-of-chain and optionally
            ;; links it from the supplied previous cluster.
            ;; ----------------------------------------------------------------
fat_alloc_chain_cluster$:
            ld      (fat_mut_prev$),de
            call    fat_find_free_cluster$
            ld      a,h
            or      l
            ret     nz
            ld      (fat_mut_new$),de

            call    fat_eoc_value$
            ex      de,hl
            ld      hl,(fat_mut_new$)
            ex      de,hl
            call    fat_set_fat_entry$
            ld      a,h
            or      l
            ret     nz

            ld      de,(fat_mut_prev$)
            ld      a,d
            or      e
            jr      z,fach_hint$
            ld      hl,(fat_mut_new$)
            call    fat_set_fat_entry$
            ld      a,h
            or      l
            ret     nz

fach_hint$:
            ld      de,(fat_mut_new$)
            push    de
            call    fat_advance_alloc_hint$
            pop     de
            ld      hl,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_zero_cluster$(<de> cluster) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; zero-fills every sector in one cluster. this is used only for
            ;; new directory clusters, not normal file-data growth.
            ;; ----------------------------------------------------------------
fat_zero_cluster$:
            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            ret     nz
            ld      (fat_mut_sector$),de

            ld      hl,#fat_sector$
            xor     a
            ld      (hl),a
            ld      d,h
            ld      e,l
            inc     de
            ld      bc,#FAT_SECTOR_SIZE - 1
            ldir

            ld      bc,#FATFS_SECTORS_PER_CL
            call    fat_fs_byte$
            ld      (fat_mut_count$),a

fzc_loop$:
            ld      de,(fat_mut_sector$)
            call    fat_write_volume_sector$
            ld      a,h
            or      l
            ret     nz

            ld      hl,(fat_mut_sector$)
            inc     hl
            ld      (fat_mut_sector$),hl
            ld      a,(fat_mut_count$)
            dec     a
            ld      (fat_mut_count$),a
            jr      nz,fzc_loop$
            ld      hl,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_update_file_dirent$(<hl> file) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; writes a file handle's start cluster and size back into its
            ;; owning 32-byte directory entry sector.
            ;; ----------------------------------------------------------------
fat_update_file_dirent$:
            ld      (fat_mut_file$),hl

            push    hl
            ld      bc,#FATFILE_DIR_SECTOR
            add     hl,bc
            call    fat_load_de$
            pop     hl
            ld      a,d
            cp      #0xff
            jr      nz,fufd_have_sector$
            ld      a,e
            cp      #0xff
            jr      nz,fufd_have_sector$
            ld      hl,#FAT_EBADF
            ret
fufd_have_sector$:
            ld      (fat_mut_sector$),de
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            ret     nz

            ld      hl,(fat_mut_file$)
            ld      bc,#FATFILE_DIR_OFFSET
            add     hl,bc
            ld      a,(hl)
            ld      l,a
            ld      h,#0
            ld      de,#fat_sector$
            add     hl,de
            ld      (fat_mut_entry$),hl

            push    hl
            ld      de,#FAT_ENTRY_CLUSTER
            add     hl,de
            push    hl
            ld      hl,(fat_mut_file$)
            call    fat_load_de$
            pop     hl
            call    fat_store_de$
            pop     hl

            ld      de,#FAT_ENTRY_SIZE32
            add     hl,de
            ex      de,hl
            ld      hl,(fat_mut_file$)
            ld      bc,#FATFILE_SIZE
            add     hl,bc
            ld      b,#4
fufd_size$:
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            djnz    fufd_size$

            ld      de,(fat_mut_sector$)
            jp      fat_write_volume_sector$

            .area   _SYSVARS

fat_mut_value$:
            .ds     2
fat_mut_step$:
            .ds     2
fat_mut_sector$:
            .ds     2
fat_mut_start$:
fat_mut_prev$:
fat_mut_file$:
            .ds     2
fat_mut_curr$:
fat_mut_new$:
fat_mut_entry$:
            .ds     2
fat_mut_count$:
            .ds     1

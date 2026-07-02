            ;; fat_create.s
            ;;
            ;; asynchronous FAT12/FAT16 file creation for PartOS.
            ;;
            ;; this module finishes the minimal file story:
            ;;   - walk an existing parent path through root/subdirectories
            ;;   - locate the first reusable 8.3 slot in that directory
            ;;   - extend subdirectories by one cluster when full
            ;;   - emit one zero-length archive file entry
            ;;
            ;; directory creation stays out of scope here. this is only the
            ;; smallest regular-file create path needed by fat_write() growth.
            ;; ----------------------------------------------------------------
            ;; 2026-06-17   tstih
            .module fat_create

            .include "fat.inc"

            .globl  fat_handle_create$

            .globl  fat_load_de$
            .globl  fat_dirent_de$
            .globl  fat_dirent_byte$
            .globl  fat_store_de$
            .globl  fat_complete_dirent$
            .globl  fat_finish_dirent$
            .globl  fat_prepare_dirent_busy$
            .globl  fat_path_next$
            .globl  fat_walk_to_parent$
            .globl  fat_read_volume_sector$
            .globl  fat_write_volume_sector$
            .globl  fat_fill_dirent_from_entry$
            .globl  fat_scan_active_dir$
            .globl  fat_scan_root$
            .globl  fat_scan_subdir$
            .globl  fat_prepare_path_req$
            .globl  fat_walk_mode$
            .globl  fat_cluster_to_sector$
            .globl  fat_get_fat_entry$
            .globl  fat_is_eoc$
            .globl  fat_alloc_chain_cluster$
            .globl  fat_zero_cluster$
            .globl  fat_write_block$
            .globl  fat_work_fs$
            .globl  fat_work_dev$
            .globl  fat_work_event$
            .globl  fat_lookup_path$
            .globl  fat_lookup_more$
            .globl  fat_lookup_dirent$
            .globl  fat_lookup_sector$
            .globl  fat_lookup_entry_off$
            .globl  fat_scan_remaining$
            .globl  fat_lookup_cluster$
            .globl  fat_name$
            .globl  fat_sector$
            .globl  fat_create_entry$
            .globl  fat_create_cluster$

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; fat_handle_create$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side regular-file creation.
            ;; ----------------------------------------------------------------
fat_handle_create$:
            call    fat_prepare_path_req$
            ld      a,h
            or      l
            jp      nz,fhc_fail_hl$
            call    fat_walk_to_parent$
            ld      a,h
            or      l
            jp      nz,fhc_fail_hl$
fhc_last$:
            ld      a,#FAT_WALK_CREATE
            ld      (fat_walk_mode$),a
            call    fat_scan_active_dir$
            ld      a,h
            or      l
            jr      z,fhc_slot_ready$

            ld      de,#FAT_ENOSPC
            ld      a,l
            cp      e
            jp      nz,fhc_fail_hl$
            ld      a,h
            cp      d
            jp      nz,fhc_fail_hl$
            ld      hl,(fat_lookup_cluster$)
            ld      a,h
            or      l
            jr      z,fhc_fail_hl$

            ld      de,(fat_lookup_cluster$)
            call    fat_alloc_chain_cluster$
            ld      a,h
            or      l
            jr      nz,fhc_fail_hl$
            ld      (fat_create_cluster$),de
            call    fat_zero_cluster$
            ld      a,h
            or      l
            jr      nz,fhc_fail_hl$
            ld      de,(fat_create_cluster$)
            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            jr      nz,fhc_fail_hl$
            ld      (fat_lookup_sector$),de
            xor     a
            ld      (fat_lookup_entry_off$),a

fhc_slot_ready$:
            ld      de,(fat_lookup_sector$)
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            jr      nz,fhc_fail_hl$

            ld      a,(fat_lookup_entry_off$)
            ld      l,a
            ld      h,#0
            ld      de,#fat_sector$
            add     hl,de
            ld      (fat_create_entry$),hl

            xor     a
            ld      b,#FAT_ENTRY_SIZE
fhc_zero_entry$:
            ld      (hl),a
            inc     hl
            djnz    fhc_zero_entry$

            ld      hl,#fat_name$
            ld      de,(fat_create_entry$)
            ld      bc,#FAT_SHORT_NAME_LEN
            ldir

            ld      hl,(fat_create_entry$)
            ld      de,#FAT_ENTRY_ATTR
            add     hl,de
            ld      (hl),#FAT_ATTR_ARCHIVE

            ld      de,(fat_lookup_sector$)
            call    fat_write_volume_sector$
            ld      a,h
            or      l
            jr      nz,fhc_fail_hl$

            ld      de,(fat_create_entry$)
            call    fat_fill_dirent_from_entry$
            ld      de,#FAT_OK
            jr      fhc_finish$

fhc_fail_hl$:
            ex      de,hl
            ld      hl,(fat_lookup_dirent$)
            push    de                  ; preserve the real failure status
            call    fat_prepare_dirent_busy$
            pop     de

fhc_finish$:
            jp      fat_finish_dirent$

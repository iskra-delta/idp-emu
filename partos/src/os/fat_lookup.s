            ;; fat_lookup.s
            ;;
            ;; FAT12/FAT16 read-only 8.3 path lookup service.
            ;;
            ;; this file resolves one DOS 8.3 path to minimal on-disk metadata.
            ;; it owns:
            ;;   - path component parsing and normalization to short names
            ;;   - FAT12/FAT16 entry decoding through one shared helper path
            ;;   - fixed-root and subdirectory scanning
            ;;   - public async lookup submission
            ;;   - worker-side execution of queued lookup requests
            ;;
            ;; the result is intentionally narrow: enough information to locate
            ;; a directory entry, identify its first cluster, size and flags,
            ;; and hand that to later file/directory operations. there is no
            ;; long-file-name handling or file data transfer here yet.
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module fat_lookup

            .include "fat.inc"

            .globl  _fat_init
            .globl  _fat_lookup

            .globl  fat_ret_clean4$
            .globl  fat_ret_einval4$
            .globl  fat_complete_stacked_dirent4$
            .globl  fat_path_submit_check$
            .globl  fat_evt_reset$
            .globl  fat_complete_dirent$
            .globl  fat_finish_dirent$
            .globl  fat_store_de$
            .globl  fat_load_de$
            .globl  fat_dirent_de$
            .globl  fat_dirent_byte$
            .globl  fat_decode_req_evfdev$
            .globl  fat_spc_shift$
            .globl  fat_prepare_dirent_busy$
            .globl  fat_req_base$
            .globl  fat_submit_path_req$
            .globl  fat_submit_frame$
            .globl  fat_fill_root_dirent$
            .globl  fat_upper$
            .globl  fat_namechar_ok$
            .globl  fat_path_next$
            .globl  fat_skip_slashes$
            .globl  fat_fs_de$
            .globl  fat_fs_byte$
            .globl  fat_read_block$
            .globl  fat_read_block_to$
            .globl  fat_read_volume_sector$
            .globl  fat_read_volume_sector_to$
            .globl  fat_cache_fdev$
            .globl  fat_cache_fsec$
            .globl  fat_xfer_buf$
            .globl  fat_cluster_valid$
            .globl  fat_cluster_to_sector$
            .globl  fat_fat_offset$
            .globl  fat_is_eoc$
            .globl  fat_get_fat_entry$
            .globl  fat_queue_req$
            .globl  fat_queue_push$
            .globl  fat_name_match$
            .globl  fat_fill_dirent_from_entry$
            .globl  fat_scan_active_dir$
            .globl  fat_scan_root$
            .globl  fat_scan_subdir$
            .globl  fat_prepare_path_req$
            .globl  fat_walk_mode$
            .globl  fat_work_fs$
            .globl  fat_work_dev$
            .globl  fat_work_event$
            .globl  fat_req_flags$
            .globl  fat_lookup_path$
            .globl  fat_lookup_more$
            .globl  fat_path_saw_dot$
            .globl  fat_char$
            .globl  fat_fat_offset_tmp$
            .globl  fat_tmp_cluster$
            .globl  fat_offset_low$
            .globl  fat_pair$
            .globl  fat_lookup_dirent$
            .globl  fat_entry_ptr$
            .globl  fat_lookup_sector$
            .globl  fat_lookup_entry_off$
            .globl  fat_scan_remaining$
            .globl  fat_lookup_cluster$
            .globl  fat_name$
            .globl  fat_sector$

            .globl  fat_handle_lookup$
            .globl  fat_walk_to_leaf$
            .globl  fat_walk_to_parent$
            .globl  fat_handle_readdir$
            .globl  _fat_readdir

            .area   _CODE
            ;; ----------------------------------------------------------------
            ;; fat_path_next$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; parses the next slash-delimited path component from fat_lookup_
            ;; path$, normalizes it to an upper-case DOS 8.3 short name in
            ;; fat_name$ and updates fat_lookup_path$ / fat_lookup_more$.
            ;; ----------------------------------------------------------------
fat_path_next$:
            ld      de,(fat_lookup_path$)
            call    fat_skip_slashes$
            ld      a,(de)
            or      a
            jp      z,fpn_invalid$

            ld      hl,#fat_name$
            ld      a,#FAT_SPACE
            ld      b,#FAT_SHORT_NAME_LEN
fpn_fill$:
            ld      (hl),a
            inc     hl
            djnz    fpn_fill$

            xor     a
            ld      (fat_lookup_more$),a
            ld      (fat_path_saw_dot$),a
            ld      b,#0                ; base-name length
            ld      c,#0                ; extension length

fpn_loop$:
            ld      a,(de)
            or      a
            jr      z,fpn_done$
            cp      #FAT_SLASH
            jr      z,fpn_done_slash$
            inc     de
            cp      #FAT_DOT
            jr      z,fpn_dot$
            call    fat_upper$
            ld      (fat_char$),a
            call    fat_namechar_ok$
            jr      c,fpn_invalid$

            ld      a,(fat_path_saw_dot$)
            or      a
            jr      nz,fpn_ext$

            ld      a,b
            cp      #8
            jr      nc,fpn_toolong$
            push    de
            ld      l,b
            ld      h,#0
            ld      de,#fat_name$
            add     hl,de
            ld      a,(fat_char$)
            ld      (hl),a
            pop     de
            inc     b
            jr      fpn_loop$

fpn_ext$:
            ld      a,c
            cp      #3
            jr      nc,fpn_toolong$
            push    de
            ld      a,c
            add     a,#8
            ld      l,a
            ld      h,#0
            ld      de,#fat_name$
            add     hl,de
            ld      a,(fat_char$)
            ld      (hl),a
            pop     de
            inc     c
            jr      fpn_loop$

fpn_dot$:
            ld      a,(fat_path_saw_dot$)
            or      a
            jr      nz,fpn_invalid$
            ld      a,#1
            ld      (fat_path_saw_dot$),a
            jr      fpn_loop$

fpn_done_slash$:
            inc     de
            call    fat_skip_slashes$
            ld      a,(de)
            or      a
            jr      z,fpn_store_done$
            ld      a,#1
            ld      (fat_lookup_more$),a
            jr      fpn_store_done$

fpn_done$:
            xor     a
            ld      (fat_lookup_more$),a

fpn_store_done$:
            ld      (fat_lookup_path$),de
            ld      a,b
            or      a
            jr      z,fpn_invalid$
            ld      hl,#FAT_OK
            ret

fpn_toolong$:
            ld      hl,#FAT_ENAMETOOLONG
            ret
fpn_invalid$:
            ld      hl,#FAT_EINVAL
            ret

            ;; ----------------------------------------------------------------
            ;; fat_read_volume_sector$(<de> rel_sector) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; adds fs->lba_base to one volume-relative sector number and reads
            ;; the resulting device block into fat_sector$.
            ;; ----------------------------------------------------------------
fat_read_volume_sector$:
            push    de
            ld      bc,#FATFS_LBA_BASE
            call    fat_fs_de$        ; de = lba_base
            pop     hl
            add     hl,de
            ex      de,hl               ; de = absolute device block
            ld      hl,(fat_work_dev$)
            call    fat_read_block$
            ret

            ;; ----------------------------------------------------------------
            ;; fat_read_volume_sector_to$(<de> rel_sector, <hl> buf) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; like fat_read_volume_sector$ but streams the block straight into
            ;; the caller's buffer <buf> instead of fat_sector$, so the FAT-sector
            ;; cache survives file/dir DATA reads (see fat_read_block_to$).
            ;; ----------------------------------------------------------------
fat_read_volume_sector_to$:
            ld      (fat_xfer_buf$),hl  ; destination = caller buffer
            push    de
            ld      bc,#FATFS_LBA_BASE
            call    fat_fs_de$        ; de = lba_base
            pop     hl
            add     hl,de
            ex      de,hl               ; de = absolute device block
            ld      hl,(fat_work_dev$)
            call    fat_read_block_to$
            ret

            ;; ----------------------------------------------------------------
            ;; fat_cluster_valid$(<de> cluster) -> <a> 0/1
            ;; ----------------------------------------------------------------
            ;; valid FAT12/16 data clusters are in the half-open range:
            ;;   [2, total_clusters + 2)
            ;; ----------------------------------------------------------------
fat_cluster_valid$:
            ld      a,d
            or      a
            jr      nz,fcv_limit$
            ld      a,e
            cp      #FAT_CLUSTER_FIRST
            jr      c,fcv_no$
fcv_limit$:
            push    de
            ld      bc,#FATFS_TOTAL_CLUSTERS
            call    fat_fs_de$        ; de = total_clusters
            ex      de,hl               ; hl = total_clusters
            inc     hl
            inc     hl                  ; hl = total_clusters + 2
            pop     de                  ; de = candidate cluster
            or      a
            sbc     hl,de
            jr      c,fcv_no$
            ld      a,h
            or      l
            jr      z,fcv_no$
            ld      a,#1
            ret
fcv_no$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_cluster_to_sector$(<de> cluster) -> <hl> rc, <de> sector
            ;; ----------------------------------------------------------------
fat_cluster_to_sector$:
            push    de
            call    fat_cluster_valid$
            pop     de
            or      a
            jr      nz,fcts_calc$
            ld      hl,#FAT_ECORRUPT
            ret

fcts_calc$:
            ld      h,d
            ld      l,e
            dec     hl
            dec     hl                  ; zero-based cluster index
            push    hl
            ld      bc,#FATFS_SECTORS_PER_CL
            call    fat_fs_byte$
            call    fat_spc_shift$
            jr      c,fcts_fail$
            ld      b,a
            pop     hl
fcts_shift$:
            ld      a,b
            or      a
            jr      z,fcts_add_data$
            add     hl,hl
            dec     b
            jr      fcts_shift$
fcts_add_data$:
            push    hl
            ld      bc,#FATFS_DATA_START
            call    fat_fs_de$        ; de = data_start
            pop     hl
            add     hl,de
            ex      de,hl
            ld      hl,#FAT_OK
            ret
fcts_fail$:
            pop     hl
            ld      hl,#FAT_ECORRUPT
            ret

            ;; ----------------------------------------------------------------
            ;; fat_fat_offset$(<de> cluster) -> <de> byte_offset
            ;; ----------------------------------------------------------------
fat_fat_offset$:
            push    de
            ld      bc,#FATFS_FAT_BITS
            call    fat_fs_byte$
            pop     de
            cp      #12
            jr      nz,ffo_16$
            ld      h,d
            ld      l,e
            srl     h
            rr      l
            add     hl,de
            ex      de,hl
            ret
ffo_16$:
            ld      h,d
            ld      l,e
            add     hl,hl
            ex      de,hl
            ret

            ;; ----------------------------------------------------------------
            ;; fat_is_eoc$(<de> value) -> <a> 0/1
            ;; ----------------------------------------------------------------
fat_is_eoc$:
            push    de
            ld      bc,#FATFS_FAT_BITS
            call    fat_fs_byte$
            pop     de
            cp      #12
            jr      nz,fie_16$
            ld      a,d
            cp      #0x0f
            jr      c,fie_no$
            jr      nz,fie_yes$
            ld      a,e
            cp      #0xf8
            jr      c,fie_no$
            jr      fie_yes$
fie_16$:
            ld      a,d
            cp      #0xff
            jr      c,fie_no$
            jr      nz,fie_yes$
            ld      a,e
            cp      #0xf8
            jr      c,fie_no$
fie_yes$:
            ld      a,#1
            ret
fie_no$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_get_fat_entry$(<de> cluster) -> <hl> rc, <de> next
            ;; ----------------------------------------------------------------
            ;; shared FAT12/FAT16 entry reader. only the nibble packing differs.
            ;; ----------------------------------------------------------------
fat_get_fat_entry$:
            ld      (fat_tmp_cluster$),de
            call    fat_fat_offset$
            ld      (fat_fat_offset_tmp$),de

            push    de
            ld      bc,#FATFS_FAT_START
            call    fat_fs_de$        ; de = fat_start
            pop     hl                  ; hl = fat byte offset
            ld      a,l
            ld      (fat_offset_low$),a
            ld      l,h
            ld      h,#0
            add     hl,de
            ex      de,hl               ; de = rel FAT sector

            ;; --- single-sector FAT cache ---
            ;; skip the disk read when fat_sector$ already holds this (dev,sector).
            push    de                  ; save rel sector
            ld      hl,(fat_cache_fsec$)
            or      a
            sbc     hl,de
            jr      nz,fgfe_read$
            ld      de,(fat_cache_fdev$)
            ld      hl,(fat_work_dev$)
            or      a
            sbc     hl,de               ; nz if dev differs or cache invalid (fdev=0)
            jr      nz,fgfe_read$
            pop     de                  ; hit: fat_sector$ already valid
            jr      fgfe_loaded$
fgfe_read$:
            pop     de                  ; de = rel FAT sector
            push    de
            call    fat_read_volume_sector$
            pop     de
            ld      a,h
            or      l
            ret     nz                  ; read error (fat_xfer_block$ left cache invalid)
            ld      (fat_cache_fsec$),de
            ld      hl,(fat_work_dev$)
            ld      (fat_cache_fdev$),hl
fgfe_loaded$:

            ld      a,(fat_offset_low$)
            ld      l,a
            ld      h,#0
            ld      de,#fat_sector$
            add     hl,de
            ld      a,(hl)
            ld      (fat_pair$),a
            ld      a,(fat_offset_low$)
            cp      #0xff
            jr      z,fgfe_cross$
            inc     hl
            ld      a,(hl)
            ld      (fat_pair$ + 1),a
            jr      fgfe_decode$

fgfe_cross$:
            ld      bc,#FATFS_FAT_START
            call    fat_fs_de$        ; de = fat_start
            ld      hl,(fat_fat_offset_tmp$)
            ld      a,h
            inc     a
            ld      l,a
            ld      h,#0
            add     hl,de
            ex      de,hl
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            ret     nz
            ld      a,(fat_sector$)
            ld      (fat_pair$ + 1),a

fgfe_decode$:
            ld      bc,#FATFS_FAT_BITS
            call    fat_fs_byte$
            cp      #12
            jr      nz,fgfe_16$

            ld      de,(fat_tmp_cluster$)
            bit     0,e
            jr      nz,fgfe_12_odd$
            ld      a,(fat_pair$)
            ld      e,a
            ld      a,(fat_pair$ + 1)
            and     #0x0f
            ld      d,a
            ld      hl,#FAT_OK
            ret

fgfe_12_odd$:
            ld      a,(fat_pair$ + 1)
            ld      d,a
            add     a,a
            add     a,a
            add     a,a
            add     a,a
            ld      e,a
            ld      a,(fat_pair$)
            rrca
            rrca
            rrca
            rrca
            and     #0x0f
            or      e
            ld      e,a
            ld      a,d
            srl     a
            srl     a
            srl     a
            srl     a
            ld      d,a
            ld      hl,#FAT_OK
            ret

fgfe_16$:
            ld      a,(fat_pair$)
            ld      e,a
            ld      a,(fat_pair$ + 1)
            ld      d,a
            ld      hl,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_name_match$(<de> entry_name, <hl> want_name)
            ;; ----------------------------------------------------------------
            ;; returns with Z set on a full 11-byte match.
            ;; ----------------------------------------------------------------
fat_name_match$:
            ld      b,#FAT_SHORT_NAME_LEN
fnm_loop$:
            ld      a,(de)
            cp      (hl)
            ret     nz
            inc     de
            inc     hl
            djnz    fnm_loop$
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_fill_dirent_from_entry$(<de> entry_ptr)
            ;; ----------------------------------------------------------------
            ;; copies the minimal metadata we care about from the matching
            ;; on-disk 32-byte directory entry into fat_lookup_dirent$.
            ;; ----------------------------------------------------------------
fat_fill_dirent_from_entry$:
            ld      (fat_entry_ptr$),de

            ld      hl,(fat_lookup_dirent$)
            push    hl
            ld      de,(fat_entry_ptr$)
            push    de
            pop     hl
            ld      bc,#FAT_ENTRY_CLUSTER
            add     hl,bc
            call    fat_load_de$        ; de = first_cluster
            pop     hl
            push    hl
            call    fat_store_de$
            pop     hl

            push    hl
            ld      de,(fat_entry_ptr$)
            push    de
            pop     hl
            ld      bc,#FAT_ENTRY_SIZE32
            add     hl,bc               ; hl = entry->size[0]
            pop     de                  ; de = result->size[0]
            inc     de
            inc     de
            ld      b,#4
ffde_size$:
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            djnz    ffde_size$

            ld      hl,(fat_lookup_dirent$)
            ld      bc,#FATDIRENT_DIR_SECTOR
            add     hl,bc
            ld      de,(fat_lookup_sector$)
            call    fat_store_de$       ; hl now points at dir_offset
            ld      a,(fat_lookup_entry_off$)
            ld      (hl),a
            inc     hl                  ; hl now points at attr
            push    hl
            ld      de,(fat_entry_ptr$)
            push    de
            pop     hl
            ld      bc,#FAT_ENTRY_ATTR
            add     hl,bc
            ld      a,(hl)
            pop     hl
            ld      (hl),a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_fill_name$()
            ;; ----------------------------------------------------------------
            ;; copies the raw 11-byte 8.3 name from the matched entry (pointer
            ;; left in fat_entry_ptr$ by fat_fill_dirent_from_entry$) into the
            ;; readdir result's name field. stays space-padded with no dot.
            ;; ----------------------------------------------------------------
fat_fill_name$:
            ld      hl,(fat_entry_ptr$)
            ld      de,(fat_lookup_dirent$)
            push    hl
            ld      hl,#FATDIRINFO_NAME
            add     hl,de
            ex      de,hl               ; de = &info->name
            pop     hl                  ; hl = entry (raw 8.3 source)
            ld      bc,#FAT_SHORT_NAME_LEN
            ldir
            ret

            ;; ----------------------------------------------------------------
            ;; fat_claim_free$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; used by create-mode directory walks after the first reusable slot
            ;; was remembered during scanning.
            ;; ----------------------------------------------------------------
fat_claim_free$:
            ld      a,(fat_free_seen$)
            or      a
            jr      z,fcf_noslot$
            ld      de,(fat_free_sector$)
            ld      (fat_lookup_sector$),de
            ld      a,(fat_free_off$)
            ld      (fat_lookup_entry_off$),a
            ld      hl,#FAT_OK
            ret
fcf_noslot$:
            ld      hl,#FAT_ENOSPC
            ret

            ;; ----------------------------------------------------------------
            ;; fat_note_free$()
            ;; ----------------------------------------------------------------
            ;; remembers the first reusable directory slot seen during a
            ;; create-mode walk. later deleted/empty entries become no-ops.
            ;; ----------------------------------------------------------------
fat_note_free$:
            ld      a,(fat_free_seen$)
            or      a
            ret     nz
            ld      a,#1
            ld      (fat_free_seen$),a
            ld      de,(fat_lookup_sector$)
            ld      (fat_free_sector$),de
            ld      a,(fat_lookup_entry_off$)
            ld      (fat_free_off$),a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_scan_sector$() -> <a> FAT_SCAN_*
            ;; ----------------------------------------------------------------
            ;; scans the currently loaded fat_sector$ for fat_name$. in create
            ;; mode it also remembers the first reusable slot.
            ;; ----------------------------------------------------------------
fat_scan_sector$:
            ld      hl,#fat_sector$
            xor     a
            ld      (fat_lookup_entry_off$),a
            ld      b,#FAT_ENTRIES_PER_SECTOR
fss_loop$:
            ld      a,(hl)
            cp      #FAT_ENTRY_END
            jr      z,fss_end$
            cp      #FAT_ENTRY_DELETED
            jr      z,fss_deleted$

            push    hl
            push    bc                  ; keep the 8-entry sector countdown
            ld      bc,#FAT_ENTRY_ATTR
            add     hl,bc
            ld      a,(hl)
            pop     bc
            pop     hl
            cp      #FAT_ATTR_LFN
            jr      z,fss_next$
            and     #FAT_ATTR_VOLUME_ID
            jr      nz,fss_next$

            ;; readdir mode does not name-match: it emits the index-th live
            ;; entry. hl still points at the current 32-byte entry here.
            ld      a,(fat_walk_mode$)
            cp      #FAT_WALK_READDIR
            jr      z,fss_readdir$

            push    hl
            pop     de                  ; de = current 32-byte dir entry
            push    de                  ; keep the entry pointer alive across
                                        ; fat_name_match$: the matcher walks hl
                                        ; through fat_name$, so without this
                                        ; restore the next-entry advance starts
                                        ; from the short-name scratch buffer
                                        ; instead of the on-disk directory slot.
            push    bc                  ; fat_name_match$ uses b internally;
                                        ; keep the sector-entry countdown intact
            ld      hl,#fat_name$
            call    fat_name_match$
            pop     bc
            pop     hl                  ; hl = current dir entry again
            jr      nz,fss_next$

            ld      a,(fat_walk_mode$)
            or      a
            jr      nz,fss_found$
            push    hl
            pop     de
            call    fat_fill_dirent_from_entry$
fss_found$:
            ld      a,#FAT_SCAN_FOUND
            ret

fss_readdir$:
            ld      de,(fat_readdir_index$)
            ld      a,d
            or      e
            jr      nz,fss_rd_skip$
            push    hl
            pop     de                  ; de = entry pointer
            call    fat_fill_dirent_from_entry$
            call    fat_fill_name$
            ld      a,#FAT_SCAN_FOUND
            ret
fss_rd_skip$:
            dec     de
            ld      (fat_readdir_index$),de
            jr      fss_next$

fss_deleted$:
            ld      a,(fat_walk_mode$)
            cp      #FAT_WALK_CREATE
            jr      nz,fss_next$
            call    fat_note_free$

fss_next$:
            ld      de,#FAT_ENTRY_SIZE
            add     hl,de
            ld      a,(fat_lookup_entry_off$)
            add     a,#FAT_ENTRY_SIZE
            ld      (fat_lookup_entry_off$),a
            djnz    fss_loop$
            ld      a,#FAT_SCAN_CONTINUE
            ret
fss_end$:
            ld      a,(fat_walk_mode$)
            cp      #FAT_WALK_CREATE
            jr      nz,fss_end_plain$
            call    fat_note_free$
fss_end_plain$:
            ld      a,#FAT_SCAN_END
            ret

            ;; ----------------------------------------------------------------
            ;; fat_scan_root$() -> <hl> rc
            ;; ----------------------------------------------------------------
fat_scan_root$:
            call    fat_scan_prepare$
            ld      bc,#FATFS_ROOT_START
            call    fat_fs_de$
            ld      (fat_lookup_sector$),de

            ld      bc,#FATFS_ROOT_DIR_SECTORS
            call    fat_fs_de$
            ld      (fat_scan_remaining$),de

fsr_loop$:
            ld      de,(fat_scan_remaining$)
            ld      a,d
            or      e
            jr      nz,fsr_have_sectors$
            call    fat_scan_miss$
            ret

fsr_have_sectors$:
            ld      de,(fat_lookup_sector$)
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            ret     nz

            call    fat_scan_sector$
            cp      #FAT_SCAN_FOUND
            jr      nz,fsr_not_found$
            call    fat_scan_found$
            ret

fsr_not_found$:
            cp      #FAT_SCAN_END
            jr      nz,fsr_next_entry$
            call    fat_scan_miss$
            ret

fsr_next_entry$:
            ld      hl,(fat_lookup_sector$)
            inc     hl
            ld      (fat_lookup_sector$),hl
            ld      hl,(fat_scan_remaining$)
            dec     hl
            ld      (fat_scan_remaining$),hl
            jr      fsr_loop$

            ;; ----------------------------------------------------------------
            ;; shared directory-scan setup and return tails used by both the
            ;; fixed-root walker and the cluster-chain subdirectory walker.
            ;; ----------------------------------------------------------------
fat_scan_prepare$:
            ld      a,(fat_walk_mode$)
            or      a
            ret     z
            xor     a
            ld      (fat_free_seen$),a
            ret

fat_scan_found$:
            ld      a,(fat_walk_mode$)
            cp      #FAT_WALK_CREATE
            ld      hl,#FAT_OK
            ret     nz                  ; lookup + readdir: a hit is success
            ld      hl,#FAT_EEXIST
            ret

fat_scan_miss$:
            ld      a,(fat_walk_mode$)
            cp      #FAT_WALK_CREATE
            ld      hl,#FAT_ENOENT
            ret     nz                  ; lookup + readdir: end of dir => ENOENT
            jp      fat_claim_free$

            ;; ----------------------------------------------------------------
            ;; fat_scan_active_dir$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; picks the current directory walker from fat_lookup_cluster$:
            ;; cluster 0 means the fixed root directory, otherwise follow the
            ;; subdirectory cluster chain.
            ;; ----------------------------------------------------------------
fat_scan_active_dir$:
            ld      hl,(fat_lookup_cluster$)
            ld      a,h
            or      l
            jr      nz,fsad_subdir$
            call    fat_scan_root$
            ret
fsad_subdir$:
            call    fat_scan_subdir$
            ret

            ;; ----------------------------------------------------------------
            ;; fat_scan_subdir$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; scans a subdirectory by following its cluster chain.
            ;; ----------------------------------------------------------------
fat_scan_subdir$:
            call    fat_scan_prepare$
            ld      bc,#FATFS_TOTAL_CLUSTERS
            call    fat_fs_de$
            ld      (fat_scan_remaining$),de

fssd_cluster$:
            ld      hl,(fat_scan_remaining$)
            ld      a,h
            or      l
            jr      z,fssd_corrupt$
            dec     hl
            ld      (fat_scan_remaining$),hl

            ld      de,(fat_lookup_cluster$)
            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            ret     nz
            ld      (fat_lookup_sector$),de

            ld      hl,(fat_work_fs$)
            ld      bc,#FATFS_SECTORS_PER_CL
            add     hl,bc
            ld      b,(hl)
fssd_sector$:
            push    bc
            ld      de,(fat_lookup_sector$)
            call    fat_read_volume_sector$
            pop     bc
            ld      a,h
            or      l
            ret     nz

            call    fat_scan_sector$
            cp      #FAT_SCAN_FOUND
            jr      nz,fssd_not_found$
            call    fat_scan_found$
            ret

fssd_not_found$:
            cp      #FAT_SCAN_END
            jr      nz,fssd_next_entry$
            call    fat_scan_miss$
            ret

fssd_next_entry$:
            ld      hl,(fat_lookup_sector$)
            inc     hl
            ld      (fat_lookup_sector$),hl
            djnz    fssd_sector$

            ld      de,(fat_lookup_cluster$)
            call    fat_get_fat_entry$
            ld      a,h
            or      l
            ret     nz
            push    de
            call    fat_is_eoc$
            pop     de
            or      a
            jr      z,fssd_advance_cluster$
            call    fat_scan_miss$
            ret

fssd_advance_cluster$:
            ld      (fat_lookup_cluster$),de
            jr      fssd_cluster$

fssd_corrupt$:
            ld      hl,#FAT_ECORRUPT
            ret

            ;; ----------------------------------------------------------------
            ;; fat_prepare_path_req$(<de> req) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; shared worker-side request decode and validation for path-based
            ;; FAT operations. callers still decide what an empty path means.
            ;; ----------------------------------------------------------------
fat_prepare_path_req$:
            call    fat_decode_req_evfdev$
            call    fat_load_de$
            ld      (fat_lookup_path$),de

            inc     hl                  ; skip req->bytes
            inc     hl
            inc     hl                  ; req->arg
            call    fat_load_de$
            ld      a,d
            or      e
            jr      z,fpp_bad$
            ld      (fat_lookup_dirent$),de
            push    de
            pop     hl
            call    fat_prepare_dirent_busy$

            ld      hl,(fat_work_fs$)
            ld      a,h
            or      l
            jr      z,fpp_bad$
            ld      de,(fat_lookup_path$)
            ld      a,d
            or      e
            jr      z,fpp_bad$
            ld      de,(fat_work_dev$)
            ld      a,d
            or      e
            jr      z,fpp_ebadf$

            ld      bc,#FATFS_MOUNTED
            call    fat_fs_byte$
            or      a
            jr      z,fpp_ebadf$

            ld      de,(fat_lookup_path$)
            call    fat_skip_slashes$
            ld      (fat_lookup_path$),de
            xor     a
            ld      (fat_lookup_cluster$),a
            ld      (fat_lookup_cluster$ + 1),a
            ld      hl,#FAT_OK
            ret
fpp_ebadf$:
            ld      hl,#FAT_EBADF
            ret
fpp_bad$:
            ld      hl,#FAT_EINVAL
            ret

            ;; ----------------------------------------------------------------
            ;; fat_handle_lookup$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side read-only path resolution. the directory walk stays
            ;; intentionally narrow: root + subdirectories, DOS 8.3 names, no
            ;; long-file-name support yet.
            ;; ----------------------------------------------------------------
fat_handle_lookup$:
            xor     a
            ld      (fat_walk_mode$),a
            call    fat_prepare_path_req$
            ld      a,h
            or      l
            jr      nz,fhl_finish_hl$
            ld      de,(fat_lookup_path$)
            ld      a,(de)
            or      a
            jr      nz,fhl_walk$

            ld      hl,(fat_lookup_dirent$)
            call    fat_fill_root_dirent$
            ld      de,#FAT_OK
            jr      fhl_finish$

fhl_walk$:
            call    fat_walk_to_leaf$
            ld      a,h
            or      l
            jr      nz,fhl_finish_hl$

fhl_success$:
            ld      a,(fat_req_flags$)
            and     #FATREQ_FLAG_FILE_ONLY
            jr      z,fhl_lookup_ok$
            ld      bc,#FATDIRENT_ATTR
            call    fat_dirent_byte$
            and     #(FAT_ATTR_DIRECTORY | FAT_ATTR_VOLUME_ID)
            jr      z,fhl_lookup_ok$
            ld      hl,#FAT_EBADF
            jr      fhl_finish_hl$
fhl_lookup_ok$:
            ld      de,#FAT_OK
            jr      fhl_finish$
fhl_finish_hl$:
            ex      de,hl
fhl_finish$:
            jp      fat_finish_dirent$

            ;; ----------------------------------------------------------------
            ;; fat_walk_to_leaf$() -> <hl> rc (0 = final component reached)
            ;; ----------------------------------------------------------------
            ;; shared component-by-component descent for lookup and the
            ;; unlink/rmdir resolvers: parse each path component, scan the
            ;; current directory, follow subdirectories, and stop at the final
            ;; component. caller must have prepared the request and ensured the
            ;; path is non-empty.
            ;; ----------------------------------------------------------------
fat_walk_to_leaf$:
fwl_loop$:
            call    fat_path_next$
            ld      a,h
            or      l
            ret     nz
            call    fat_scan_active_dir$
            ld      a,h
            or      l
            ret     nz
            ld      a,(fat_lookup_more$)
            or      a
            ret     z
            ld      bc,#FATDIRENT_ATTR
            call    fat_dirent_byte$
            and     #FAT_ATTR_DIRECTORY
            jr      z,fwl_enoent$
            ld      hl,(fat_lookup_dirent$)
            call    fat_load_de$
            ld      (fat_lookup_cluster$),de
            jr      fwl_loop$
fwl_enoent$:
            ld      hl,#FAT_ENOENT
            ret

            ;; ----------------------------------------------------------------
            ;; fat_walk_to_parent$() -> <hl> rc (0 = final component's parent)
            ;; ----------------------------------------------------------------
            ;; like fat_walk_to_leaf$ but STOPS before the final component so
            ;; create/mkdir can act on the leaf name left in fat_name$. sets
            ;; lookup walk mode and rejects the empty (root) path. caller must
            ;; have prepared the request.
            ;; ----------------------------------------------------------------
fat_walk_to_parent$:
            xor     a
            ld      (fat_walk_mode$),a
            ld      de,(fat_lookup_path$)
            ld      a,(de)
            or      a
            jr      nz,fwp_loop$
            ld      hl,#FAT_EINVAL
            ret
fwp_loop$:
            call    fat_path_next$
            ld      a,h
            or      l
            ret     nz
            ld      a,(fat_lookup_more$)
            or      a
            ret     z
            call    fat_scan_active_dir$
            ld      a,h
            or      l
            ret     nz
            ld      bc,#FATDIRENT_ATTR
            call    fat_dirent_byte$
            and     #FAT_ATTR_DIRECTORY
            jr      z,fwp_ebadf$
            ld      hl,(fat_lookup_dirent$)
            call    fat_load_de$
            ld      (fat_lookup_cluster$),de
            jr      fwp_loop$
fwp_ebadf$:
            ld      hl,#FAT_EBADF
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _fat_lookup(<hl> fs, <de> path, <stack> dirent, event)
            ;; ----------------------------------------------------------------
            ;; queues one read-only metadata lookup. arguments are validated up
            ;; front, the caller-visible result is marked busy immediately and
            ;; the real directory walk happens later on the shared FAT worker.
            ;; ----------------------------------------------------------------
_fat_lookup::
            call    fat_path_submit_check$

            ld      (fat_submit_frame$),hl
            ld      (fat_submit_frame$ + 2),de

            push    de                  ; keep path while we mark the result busy
            push    hl                  ; keep fs while we mark the result busy
            ld      hl,#8
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = stacked dirent pointer (sp+8)
            call    fat_prepare_dirent_busy$
            pop     hl                  ; restore fs
            pop     de                  ; restore path

            ld      de,#0x0200          ; d = LOOKUP op, e = flags 0
            jp      fat_submit_path_req$

            ;; ----------------------------------------------------------------
            ;; fat_handle_readdir$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side directory enumeration. this first cut lists the
            ;; fixed root directory; info->index (FATDIRINFO_INDEX) selects the
            ;; live entry and fat_scan_sector$ emits it in FAT_WALK_READDIR mode.
            ;; ----------------------------------------------------------------
fat_handle_readdir$:
            xor     a
            ld      (fat_walk_mode$),a
            call    fat_prepare_path_req$
            ld      a,h
            or      l
            jr      nz,fhrd_finish_hl$

            ld      hl,(fat_lookup_dirent$)
            ld      (fat_readdir_result$),hl
            ld      bc,#FATDIRINFO_INDEX
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      (fat_readdir_index$),de

            ;; "/" enumerates the fixed root directly. any non-empty path must
            ;; first resolve to a directory so readdir walks that directory
            ;; instead of always falling back to the root scanner.
            ld      de,(fat_lookup_path$)
            ld      a,(de)
            or      a
            jr      z,fhrd_scan$

            ;; Resolve the directory being listed into private scratch. The
            ;; caller's fat_dirinfo_t is only for the emitted entry.
            ld      hl,#fat_readdir_dirent$
            ld      (fat_lookup_dirent$),hl
            call    fat_prepare_dirent_busy$
            call    fat_walk_to_leaf$
            ld      a,h
            or      l
            jr      nz,fhrd_finish_restore_hl$

            ld      bc,#FATDIRENT_ATTR
            call    fat_dirent_byte$
            and     #(FAT_ATTR_DIRECTORY | FAT_ATTR_VOLUME_ID)
            cp      #FAT_ATTR_DIRECTORY
            jr      nz,fhrd_badf$

            ld      hl,(fat_lookup_dirent$)
            call    fat_load_de$
            ld      (fat_lookup_cluster$),de
            ld      hl,(fat_readdir_result$)
            ld      (fat_lookup_dirent$),hl

fhrd_scan$:
            ld      a,#FAT_WALK_READDIR
            ld      (fat_walk_mode$),a
            call    fat_scan_active_dir$
            jr      fhrd_finish_hl$

fhrd_badf$:
            ld      hl,#FAT_EBADF
fhrd_finish_restore_hl$:
            ld      de,(fat_readdir_result$)
            ld      (fat_lookup_dirent$),de
fhrd_finish_hl$:
            ex      de,hl               ; de = rc
            jp      fat_finish_dirent$

            ;; ----------------------------------------------------------------
            ;; <de> <= _fat_readdir(<hl> fs, <de> path, <stack> info, event)
            ;; ----------------------------------------------------------------
            ;; queues one directory-enumeration request. mirrors _fat_lookup;
            ;; info->index selects the entry and the result lands in info.
            ;; ----------------------------------------------------------------
_fat_readdir::
            call    fat_path_submit_check$

            ld      (fat_submit_frame$),hl
            ld      (fat_submit_frame$ + 2),de

            push    de                  ; keep path while we mark the result busy
            push    hl                  ; keep fs while we mark the result busy
            ld      hl,#8
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = stacked info pointer (sp+8)
            call    fat_prepare_dirent_busy$
            pop     hl                  ; restore fs
            pop     de                  ; restore path

            ld      de,#0x0600          ; d = READDIR op, e = flags 0
            jp      fat_submit_path_req$

            .area   _SYSVARS

fat_walk_mode$:
            .ds     1
fat_req_flags$:
fat_free_seen$:
            .ds     1
fat_free_sector$:
            .ds     2
fat_free_off$:
            .ds     1
fat_readdir_index$:
            .ds     2
fat_readdir_result$:
            .ds     2
fat_readdir_dirent$:
            .ds     FATDIRENT_SIZEOF

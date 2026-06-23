            ;; fat_dir.s
            ;;
            ;; FAT12/FAT16 directory + delete operations for PartOS:
            ;;   - fat_unlink()  delete one regular file
            ;;   - fat_mkdir()   create one directory (with . and .. entries)
            ;;   - fat_rmdir()   remove one empty directory
            ;;
            ;; these finish the mutating side of the FAT story. all three are
            ;; async, queued on the shared FAT worker, and reuse the path-walk,
            ;; scan, FAT-mutate and completion helpers from the other modules.
            ;; they take a fat_dirent_t result object purely so the caller can
            ;; wait on the event and read result->status (mkdir also leaves the
            ;; new directory's metadata there).
            ;; ----------------------------------------------------------------
            ;; 2026-06-20   tstih
            .module fat_dir

            .include "fat.inc"

            .globl  _fat_unlink
            .globl  _fat_mkdir
            .globl  _fat_rmdir
            .globl  fat_handle_unlink$
            .globl  fat_handle_mkdir$
            .globl  fat_handle_rmdir$

            ;; shared helpers pulled from the other FAT modules
            .globl  fat_load_de$
            .globl  fat_dirent_de$
            .globl  fat_dirent_byte$
            .globl  fat_fs_de$
            .globl  fat_fs_byte$
            .globl  fat_store_de$
            .globl  fat_complete_dirent$
            .globl  fat_finish_dirent$
            .globl  fat_prepare_dirent_busy$
            .globl  fat_path_submit_check$
            .globl  fat_submit_path_req$
            .globl  fat_submit_frame$
            .globl  fat_prepare_path_req$
            .globl  fat_path_next$
            .globl  fat_scan_active_dir$
            .globl  fat_walk_to_leaf$
            .globl  fat_walk_to_parent$
            .globl  fat_fill_dirent_from_entry$
            .globl  fat_read_volume_sector$
            .globl  fat_write_volume_sector$
            .globl  fat_cluster_to_sector$
            .globl  fat_cluster_valid$
            .globl  fat_get_fat_entry$
            .globl  fat_is_eoc$
            .globl  fat_set_fat_entry$
            .globl  fat_alloc_chain_cluster$
            .globl  fat_zero_cluster$
            .globl  fat_walk_mode$
            .globl  fat_lookup_path$
            .globl  fat_lookup_more$
            .globl  fat_lookup_dirent$
            .globl  fat_lookup_sector$
            .globl  fat_lookup_entry_off$
            .globl  fat_lookup_cluster$
            .globl  fat_name$
            .globl  fat_sector$
            .globl  fat_create_entry$
            .globl  fat_create_cluster$
            .globl  fat_work_fs$
            .globl  fat_work_event$

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; fat_free_chain$(<de> first_cluster) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; frees an entire cluster chain by zeroing each FAT entry. the next
            ;; link is read before the current cluster is released so the walk is
            ;; never lost. a 0 / out-of-range head means "no chain" and is OK.
            ;; ----------------------------------------------------------------
fat_free_chain$:
            ld      (fat_dir_cluster$),de
ffc_loop$:
            ld      de,(fat_dir_cluster$)
            ld      a,d
            or      e
            jr      z,ffc_ok$
            push    de
            call    fat_cluster_valid$
            pop     de
            or      a
            jr      z,ffc_ok$           ; ran off into eoc / invalid: stop

            call    fat_get_fat_entry$  ; de = next link, hl = rc
            ld      a,h
            or      l
            ret     nz
            ld      (fat_dir_next$),de
            push    de
            call    fat_is_eoc$         ; a = 1 when next == end-of-chain
            pop     de
            ld      (fat_dir_eoc$),a

            ld      de,(fat_dir_cluster$)
            ld      hl,#0x0000
            call    fat_set_fat_entry$  ; release the current cluster
            ld      a,h
            or      l
            ret     nz

            ld      a,(fat_dir_eoc$)
            or      a
            jr      nz,ffc_ok$
            ld      de,(fat_dir_next$)
            ld      (fat_dir_cluster$),de
            jr      ffc_loop$
ffc_ok$:
            ld      hl,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_mark_dirent_deleted$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; rewrites the on-disk 32-byte entry referenced by fat_lookup_
            ;; dirent$ (dir_sector / dir_offset) with a leading 0xE5 deleted
            ;; marker. the synthetic root (dir_sector 0xffff) cannot be removed.
            ;; ----------------------------------------------------------------
fat_mark_dirent_deleted$:
            ld      bc,#FATDIRENT_DIR_SECTOR
            call    fat_dirent_de$        ; de = dir_sector
            ld      a,d
            cp      #0xff
            jr      nz,fmdd_go$
            ld      a,e
            cp      #0xff
            jr      z,fmdd_badf$
fmdd_go$:
            push    de
            call    fat_read_volume_sector$
            pop     de
            ld      a,h
            or      l
            ret     nz

            ld      bc,#FATDIRENT_DIR_OFFSET
            call    fat_dirent_byte$              ; dir_offset
            ld      l,a
            ld      h,#0
            ld      de,#fat_sector$
            add     hl,de
            ld      (hl),#FAT_ENTRY_DELETED

            ld      bc,#FATDIRENT_DIR_SECTOR
            call    fat_dirent_de$
            jp      fat_write_volume_sector$
fmdd_badf$:
            ld      hl,#FAT_EBADF
            ret

            ;; ----------------------------------------------------------------
            ;; fat_scan_dir_used$() -> <a> 0 none / 1 used / 2 end
            ;; ----------------------------------------------------------------
            ;; inspects the loaded fat_sector$ for a live directory entry that is
            ;; not "." or "..". 0x00 ends the directory; 0xE5 and dot entries are
            ;; skipped; anything else means the directory is not empty.
            ;; ----------------------------------------------------------------
fat_scan_dir_used$:
            ld      hl,#fat_sector$
            ld      b,#FAT_ENTRIES_PER_SECTOR
fsdu_loop$:
            ld      a,(hl)
            or      a
            jr      z,fsdu_end$
            cp      #FAT_ENTRY_DELETED
            jr      z,fsdu_next$
            cp      #FAT_DOT
            jr      z,fsdu_next$
            ld      a,#1
            ret
fsdu_next$:
            ld      de,#FAT_ENTRY_SIZE
            add     hl,de
            djnz    fsdu_loop$
            xor     a
            ret
fsdu_end$:
            ld      a,#2
            ret

            ;; ----------------------------------------------------------------
            ;; fat_dir_is_empty$(<de> first_cluster) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; walks a directory's cluster chain and returns FAT_OK if it holds
            ;; no entries besides "." / ".." (and free slots), FAT_ENOTEMPTY
            ;; otherwise, or an I/O error code.
            ;; ----------------------------------------------------------------
fat_dir_is_empty$:
            ld      (fat_dir_cluster$),de
fdie_cluster$:
            ld      de,(fat_dir_cluster$)
            ld      a,d
            or      e
            jr      z,fdie_empty$
            push    de
            call    fat_cluster_valid$
            pop     de
            or      a
            jr      z,fdie_empty$

            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            ret     nz
            ld      (fat_dir_sector$),de
            ld      bc,#FATFS_SECTORS_PER_CL
            call    fat_fs_byte$
            ld      (fat_dir_secs$),a
fdie_sector$:
            ld      de,(fat_dir_sector$)
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            ret     nz
            call    fat_scan_dir_used$
            cp      #1
            jr      z,fdie_notempty$
            cp      #2
            jr      z,fdie_empty$

            ld      hl,(fat_dir_sector$)
            inc     hl
            ld      (fat_dir_sector$),hl
            ld      a,(fat_dir_secs$)
            dec     a
            ld      (fat_dir_secs$),a
            jr      nz,fdie_sector$

            ld      de,(fat_dir_cluster$)
            call    fat_get_fat_entry$
            ld      a,h
            or      l
            ret     nz
            push    de
            call    fat_is_eoc$
            pop     de
            or      a
            jr      nz,fdie_empty$
            ld      (fat_dir_cluster$),de
            jr      fdie_cluster$
fdie_empty$:
            ld      hl,#FAT_OK
            ret
fdie_notempty$:
            ld      hl,#FAT_ENOTEMPTY
            ret

            ;; ----------------------------------------------------------------
            ;; fat_resolve_target$(<de> req) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; shared "walk a full path to an existing leaf" used by unlink and
            ;; rmdir. decodes the request, refuses the empty (root) path, then
            ;; walks component by component in LOOKUP mode, descending through
            ;; intermediate directories. on success fat_lookup_dirent$ holds the
            ;; leaf metadata and dir_sector/offset point at its on-disk entry.
            ;; ----------------------------------------------------------------
fat_resolve_target$:
            xor     a
            ld      (fat_walk_mode$),a
            call    fat_prepare_path_req$
            ld      a,h
            or      l
            ret     nz
            ld      de,(fat_lookup_path$)
            ld      a,(de)
            or      a
            jr      z,frt_einval$
            jp      fat_walk_to_leaf$   ; shared component descent (tail call)
frt_einval$:
            ld      hl,#FAT_EINVAL
            ret

            ;; ----------------------------------------------------------------
            ;; fat_handle_unlink$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side regular-file delete: resolve the file, free its
            ;; cluster chain, then mark its directory entry deleted.
            ;; ----------------------------------------------------------------
fat_handle_unlink$:
            call    fat_resolve_target$
            ld      a,h
            or      l
            jr      nz,fhu_finish_hl$

            ld      bc,#FATDIRENT_ATTR
            call    fat_dirent_byte$
            and     #(FAT_ATTR_DIRECTORY | FAT_ATTR_VOLUME_ID)
            jr      nz,fhu_ebadf$

            ld      hl,(fat_lookup_dirent$)
            call    fat_load_de$
            call    fat_free_chain$
            ld      a,h
            or      l
            jr      nz,fhu_finish_hl$

            call    fat_mark_dirent_deleted$
            ld      a,h
            or      l
            jr      nz,fhu_finish_hl$
            ld      de,#FAT_OK
            jr      fhu_finish$
fhu_ebadf$:
            ld      hl,#FAT_EBADF
fhu_finish_hl$:
            ex      de,hl
fhu_finish$:
            jp      fat_finish_dirent$

            ;; ----------------------------------------------------------------
            ;; fat_handle_rmdir$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side directory delete: resolve the directory, refuse if it
            ;; is not a directory or not empty, free its chain, then mark its
            ;; entry deleted.
            ;; ----------------------------------------------------------------
fat_handle_rmdir$:
            call    fat_resolve_target$
            ld      a,h
            or      l
            jr      nz,fhr_finish_hl$

            ld      bc,#FATDIRENT_ATTR
            call    fat_dirent_byte$
            and     #FAT_ATTR_DIRECTORY
            jr      z,fhr_ebadf$

            ld      hl,(fat_lookup_dirent$)
            call    fat_load_de$
            call    fat_dir_is_empty$
            ld      a,h
            or      l
            jr      nz,fhr_finish_hl$   ; ENOTEMPTY or I/O error

            ld      hl,(fat_lookup_dirent$)
            call    fat_load_de$
            call    fat_free_chain$
            ld      a,h
            or      l
            jr      nz,fhr_finish_hl$

            call    fat_mark_dirent_deleted$
            ld      a,h
            or      l
            jr      nz,fhr_finish_hl$
            ld      de,#FAT_OK
            jr      fhr_finish$
fhr_ebadf$:
            ld      hl,#FAT_EBADF
fhr_finish_hl$:
            ex      de,hl
fhr_finish$:
            jp      fat_finish_dirent$

            ;; ----------------------------------------------------------------
            ;; fat_handle_mkdir$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side directory create: walk the parent path, claim a free
            ;; slot, allocate + zero a cluster for the new directory, seed it
            ;; with "." / "..", then write the parent's DIRECTORY entry.
            ;; ----------------------------------------------------------------
fat_handle_mkdir$:
            call    fat_prepare_path_req$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$
            call    fat_walk_to_parent$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$

fmk_last$:
            ;; parent cluster (0 for the fixed root) becomes ".."
            ld      de,(fat_lookup_cluster$)
            ld      (fat_dir_parentcl$),de

            ld      a,#FAT_WALK_CREATE
            ld      (fat_walk_mode$),a
            call    fat_scan_active_dir$    ; FAT_OK + free slot, or EEXIST/ENOSPC
            ld      a,h
            or      l
            jr      z,fmk_have_slot$

            ld      de,#FAT_ENOSPC
            ld      a,l
            cp      e
            jp      nz,fmk_fail_hl$
            ld      a,h
            cp      d
            jp      nz,fmk_fail_hl$
            ld      hl,(fat_lookup_cluster$)
            ld      a,h
            or      l
            jp      z,fmk_fail_hl$          ; fixed-root full: ENOSPC

            ld      de,(fat_lookup_cluster$)
            call    fat_alloc_chain_cluster$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$
            ld      (fat_create_cluster$),de
            call    fat_zero_cluster$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$
            ld      de,(fat_create_cluster$)
            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$
            ld      (fat_lookup_sector$),de
            xor     a
            ld      (fat_lookup_entry_off$),a

fmk_have_slot$:
            ;; allocate + zero a standalone cluster for the new directory
            ld      de,#0x0000
            call    fat_alloc_chain_cluster$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$
            ld      (fat_dir_newcl$),de
            call    fat_zero_cluster$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$

            ;; seed the new cluster's first sector with "." and ".."
            ld      hl,#fat_sector$
            xor     a
            ld      (hl),a
            ld      d,h
            ld      e,l
            inc     de
            ld      bc,#FAT_SECTOR_SIZE - 1
            ldir

            ld      hl,#fat_sector$
            ld      (hl),#FAT_DOT
            inc     hl
            ld      b,#FAT_SHORT_NAME_LEN - 1
            ld      a,#FAT_SPACE
fmk_dot_pad$:
            ld      (hl),a
            inc     hl
            djnz    fmk_dot_pad$
            ld      (hl),#FAT_ATTR_DIRECTORY        ; entry+11 = attr
            ld      hl,#fat_sector$ + FAT_ENTRY_CLUSTER
            ld      de,(fat_dir_newcl$)
            call    fat_store_de$

            ld      hl,#fat_sector$ + FAT_ENTRY_SIZE
            ld      (hl),#FAT_DOT
            inc     hl
            ld      (hl),#FAT_DOT
            inc     hl
            ld      b,#FAT_SHORT_NAME_LEN - 2
            ld      a,#FAT_SPACE
fmk_dotdot_pad$:
            ld      (hl),a
            inc     hl
            djnz    fmk_dotdot_pad$
            ld      (hl),#FAT_ATTR_DIRECTORY
            ld      hl,#fat_sector$ + FAT_ENTRY_SIZE + FAT_ENTRY_CLUSTER
            ld      de,(fat_dir_parentcl$)
            call    fat_store_de$

            ld      de,(fat_dir_newcl$)
            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$
            call    fat_write_volume_sector$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$

            ;; write the parent directory's new DIRECTORY entry
            ld      de,(fat_lookup_sector$)
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$

            ld      a,(fat_lookup_entry_off$)
            ld      l,a
            ld      h,#0
            ld      de,#fat_sector$
            add     hl,de
            ld      (fat_create_entry$),hl
            xor     a
            ld      b,#FAT_ENTRY_SIZE
fmk_zero$:
            ld      (hl),a
            inc     hl
            djnz    fmk_zero$

            ld      hl,#fat_name$
            ld      de,(fat_create_entry$)
            ld      bc,#FAT_SHORT_NAME_LEN
            ldir

            ld      hl,(fat_create_entry$)
            ld      bc,#FAT_ENTRY_ATTR
            add     hl,bc
            ld      (hl),#FAT_ATTR_DIRECTORY
            ld      hl,(fat_create_entry$)
            ld      bc,#FAT_ENTRY_CLUSTER
            add     hl,bc
            ld      de,(fat_dir_newcl$)
            call    fat_store_de$

            ld      de,(fat_lookup_sector$)
            call    fat_write_volume_sector$
            ld      a,h
            or      l
            jp      nz,fmk_fail_hl$

            ld      de,(fat_create_entry$)
            call    fat_fill_dirent_from_entry$
            ld      de,#FAT_OK
            jr      fmk_finish$
fmk_fail_hl$:
            ex      de,hl
fmk_finish$:
            jp      fat_finish_dirent$

            ;; ----------------------------------------------------------------
            ;; public async submit entry points. all mirror _fat_lookup: keep
            ;; fs/path in fat_submit_frame$, mark the result busy, then hand the
            ;; packed op byte to the shared path-submit tail.
            ;;   <de> <= _fat_xxx(<hl> fs, <de> path, <stack> result, event)
            ;; ----------------------------------------------------------------
_fat_unlink::
            ld      a,#FATREQ_OP_UNLINK
            jr      fat_dir_submit$
_fat_mkdir::
            ld      a,#FATREQ_OP_MKDIR
            jr      fat_dir_submit$
_fat_rmdir::
            ld      a,#FATREQ_OP_RMDIR

fat_dir_submit$:
            ld      (fat_dir_op$),a     ; stash op (regs don't survive the helpers)
            call    fat_path_submit_check$
            ld      (fat_submit_frame$),hl
            ld      (fat_submit_frame$ + 2),de
            push    de
            push    hl
            ld      hl,#8
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = stacked result pointer (sp+8)
            call    fat_prepare_dirent_busy$
            pop     hl
            pop     de
            ld      a,(fat_dir_op$)
            ld      d,a
            ld      e,#0                ; de = op<<8 | flags 0
            jp      fat_submit_path_req$

            .area   _SYSVARS

fat_dir_cluster$:
            .ds     2
fat_dir_next$:
            .ds     2
fat_dir_eoc$:
            .ds     1
fat_dir_sector$:
            .ds     2
fat_dir_secs$:
            .ds     1
fat_dir_newcl$:
            .ds     2
fat_dir_parentcl$:
            .ds     2
fat_dir_op$:
            .ds     1

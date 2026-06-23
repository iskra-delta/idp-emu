            ;; fat_file.s
            ;;
            ;; block-oriented FAT12/FAT16 file open/create/read/write.
            ;;
            ;; this module deliberately stays narrow:
            ;;   - fat_open() and fat_create() share one tiny path-submit path
            ;;   - fat_read() / fat_write() move data in 256-byte blocks
            ;;   - fat_write() can extend the file by whole sectors/clusters
            ;;
            ;; that keeps the file-data layer small and lines it up with the
            ;; underlying block-device ABI while still finishing the basic file
            ;; operations expected from the OS layer.
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module fat_file

            .include "fat.inc"

            .globl  _fat_lookup
            .globl  _fat_init
            .globl  _fat_open
            .globl  _fat_create
            .globl  _fat_read
            .globl  _fat_write

            .globl  fat_ret_clean4$
            .globl  fat_ret_einval4$
            .globl  fat_alloc_req$
            .globl  fat_complete_stacked_dirent4$
            .globl  fat_path_submit_check$
            .globl  fat_evt_reset$
            .globl  fat_complete_dirent$
            .globl  fat_store_de$
            .globl  fat_load_de$
            .globl  fat_fs_de$
            .globl  fat_fs_byte$
            .globl  fat_decode_req_evfdev$
            .globl  fat_prepare_dirent_busy$
            .globl  fat_queue_req$
            .globl  fat_queue_push$
            .globl  fat_req_base$
            .globl  fat_submit_path_req$
            .globl  fat_submit_frame$
            .globl  fat_read_volume_sector$
            .globl  fat_write_volume_sector$
            .globl  fat_cluster_to_sector$
            .globl  fat_is_eoc$
            .globl  fat_get_fat_entry$
            .globl  fat_spc_shift$
            .globl  fat_alloc_chain_cluster$
            .globl  fat_update_file_dirent$
            .globl  fat_work_fs$
            .globl  fat_work_dev$
            .globl  fat_work_event$
            .globl  fat_sector$
            .globl  fat_file_ptr$
            .globl  fat_file_buf$
            .globl  fat_file_pos_secs$
            .globl  fat_file_size_secs$
            .globl  fat_file_end_secs$
            .globl  fat_file_cluster$
            .globl  fat_file_tmp16$
            .globl  fat_file_secs_left$
            .globl  fat_file_sec_in_cluster$
            .globl  fat_file_spc$
            .globl  fat_file_shift$
            .globl  fat_file_is_write$
            .globl  fat_file_at_boundary$

            .globl  fat_handle_read$
            .globl  fat_handle_write$

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; fat_locate_cluster$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; maps file position (in 256-byte sectors) to current cluster and
            ;; intra-cluster sector.
            ;; ----------------------------------------------------------------
fat_locate_cluster$:
            ld      hl,(fat_file_pos_secs$)
            ld      a,(fat_file_spc$)
            dec     a
            and     l
            ld      (fat_file_sec_in_cluster$),a

            ld      hl,(fat_file_pos_secs$)
            ld      a,(fat_file_shift$)
flc_shift$:
            or      a
            jr      z,flc_index_ready$
            srl     h
            rr      l
            dec     a
            jr      flc_shift$
flc_index_ready$:
            ld      (fat_file_tmp16$),hl

            ld      hl,(fat_file_ptr$)
            call    fat_load_de$
            ld      (fat_file_cluster$),de
            ld      a,d
            or      e
            jr      nz,flc_have_head$
            ld      hl,(fat_file_tmp16$)
            ld      a,h
            or      l
            jr      nz,flc_bad$
            ld      hl,#FAT_OK
            ret

flc_have_head$:
flc_walk$:
            ld      hl,(fat_file_tmp16$)
            ld      a,h
            or      l
            jr      z,flc_ok$
            ld      de,(fat_file_cluster$)
            call    fat_get_fat_entry$
            ld      a,h
            or      l
            ret     nz
            push    de
            call    fat_is_eoc$
            pop     de
            or      a
            jr      nz,flc_bad$
            ld      (fat_file_cluster$),de
            ld      hl,(fat_file_tmp16$)
            dec     hl
            ld      (fat_file_tmp16$),hl
            jr      flc_walk$

flc_ok$:
            ld      hl,#FAT_OK
            ret
flc_bad$:
            ld      hl,#FAT_ECORRUPT
            ret

            ;; ----------------------------------------------------------------
            ;; fat_store_file_pos$()
            ;; ----------------------------------------------------------------
fat_store_file_pos$:
            ld      hl,(fat_file_ptr$)
            ld      bc,#FATFILE_POS
            add     hl,bc
            xor     a
            ld      (hl),a
            inc     hl
            ld      de,(fat_file_pos_secs$)
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            ld      (hl),a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_store_file_first_cluster$(<hl> file, <de> cluster)
            ;; fat_store_file_size$(<hl> file, <de> sectors)
            ;; ----------------------------------------------------------------
fat_store_file_first_cluster$:
            push    hl
            call    fat_store_de$
            pop     hl
            ret

fat_store_file_size$:
            push    hl
            ld      bc,#FATFILE_SIZE
            add     hl,bc
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            ld      (hl),a
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; shared submit path for fat_open() / fat_create().
            ;; ----------------------------------------------------------------
_fat_open::
            ld      bc,#0x0201
            jr      fat_file_path_submit$

_fat_create::
            ld      bc,#0x0500

fat_file_path_submit$:
            call    fat_path_submit_check$

            ld      (fat_submit_frame$),hl
            ld      (fat_submit_frame$ + 2),de

            push    bc                  ; op/flags
            push    de                  ; path
            push    hl                  ; fs

            ld      hl,#10
            add     hl,sp
            call    fat_load_de$
            ld      (fat_file_ptr$),de    ; stacked file*

            ld      hl,(fat_submit_frame$)
            push    hl                  ; fs value
            ld      hl,(fat_file_ptr$)
            ld      bc,#FATFILE_FS
            add     hl,bc               ; hl = &file->fs
            pop     de                  ; de = fs
            call    fat_store_de$

            ld      hl,(fat_file_ptr$)
            call    fat_prepare_dirent_busy$

            ;; freshly returned handles always start at byte 0. without an
            ;; explicit clear here, file->pos kept stale garbage and the first
            ;; block read/write was rejected as FAT_ENOTSUP by the alignment
            ;; checks in fat_handle_io$.
            ld      hl,(fat_file_ptr$)
            ld      bc,#FATFILE_POS
            add     hl,bc
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a

            pop     hl                  ; drop saved fs
            pop     de                  ; drop saved path
            pop     bc                  ; op/flags
            ld      d,b
            ld      e,c
            jp      fat_submit_path_req$

            ;; ----------------------------------------------------------------
            ;; fat_file_submit$(<a> op, <hl> file, <de> buf, <stack> bytes,event)
            ;; ----------------------------------------------------------------
fat_file_submit$:
            ;; sdcc clean4 frame on entry: [sp]=ret [sp+2]=bytes [sp+4]=event
            ;; a=op, hl=file, de=buf. validate the pointers first (while the
            ;; stack is still the bare caller frame so the einval4 exit stays
            ;; aligned), then build a [op][file][buf] save frame. file/buf MUST
            ;; be saved before fat_evt_reset$, _fat_init and fat_alloc_req$ run:
            ;; every one of those clobbers hl/de (fat_evt_reset$ in particular
            ;; returns de=event), so the old code that pushed the post-reset
            ;; registers leaked two words onto the worker stack and made the
            ;; final fat_ret_clean4$ pop a garbage return address.
            push    af                  ; hold op across the pointer tests
            ld      a,h
            or      l
            jr      z,ffs_invalid$
            ld      a,d
            or      e
            jr      nz,ffs_valid$

ffs_invalid$:
            pop     bc                  ; drop saved op
            jp      fat_ret_einval4$

ffs_valid$:
            pop     af                  ; a = op again, sp back to caller frame
            push    de                  ; [sp+4] = buf
            push    hl                  ; [sp+2] = file
            push    af                  ; [sp+0] = op (op byte at sp+1)
            ;; frame: [sp]=op [sp+2]=file [sp+4]=buf [sp+6]=ret
            ;;        [sp+8]=bytes [sp+10]=event

            ;; mark the caller-visible file result busy: file->status = EBUSY
            ld      hl,#2
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = file
            ld      bc,#FATFILE_STATUS
            add     hl,bc
            ld      (hl),#<FAT_EBUSY
            inc     hl
            ld      (hl),#>FAT_EBUSY

            ;; reset the completion event so the caller can wait on it
            ld      hl,#10
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = event
            call    fat_evt_reset$

            call    _fat_init
            ld      a,d
            or      e
            jr      nz,ffs_fail_de$     ; de = init rc

            call    fat_alloc_req$      ; ix = req, Z set on failure
            jr      nz,ffs_pack$
            ld      de,#FAT_ENOMEM
            jr      ffs_fail_de$

ffs_pack$:
            ld      hl,#1
            add     hl,sp
            ld      b,(hl)              ; b = op
            call    fat_req_base$       ; hl = &req->op (ix preserved)
            ld      (hl),b              ; req->op
            inc     hl
            ld      (hl),#0             ; req->flags = 0
            inc     hl                  ; hl = &req->event

            push    hl
            ld      hl,#12
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = stacked event
            pop     hl
            call    fat_store_de$       ; req->event, hl -> &req->fs

            push    hl
            ld      hl,#4
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = file
            ld      bc,#FATFILE_FS
            add     hl,bc
            call    fat_load_de$        ; de = file->fs
            pop     hl
            call    fat_store_de$       ; req->fs (de still = fs), hl -> &req->dev

            push    hl
            ld      h,d
            ld      l,e                 ; hl = fs
            ld      bc,#FATFS_DEV
            add     hl,bc
            call    fat_load_de$        ; de = fs->dev
            pop     hl
            call    fat_store_de$       ; req->dev, hl -> &req->buf

            push    hl
            ld      hl,#6
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = buf
            pop     hl
            call    fat_store_de$       ; req->buf, hl -> &req->bytes

            push    hl
            ld      hl,#10
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = stacked bytes
            pop     hl
            call    fat_store_de$       ; req->bytes, hl -> &req->arg

            push    hl
            ld      hl,#4
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = file
            pop     hl
            call    fat_store_de$       ; req->arg = file

            call    fat_queue_req$      ; de = FAT_OK
            pop     bc                  ; drop op
            pop     bc                  ; drop file
            pop     bc                  ; drop buf
            jp      fat_ret_clean4$

ffs_fail_de$:
            ;; de = status code; signal the file result, then unwind the save
            ;; frame and return through the shared clean4 tail.
            ld      hl,#10
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked event
            ld      hl,#2
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = file
            push    de                  ; preserve sdcc rc across completion
            call    fat_complete_dirent$
            pop     de
            pop     bc                  ; drop op
            pop     bc                  ; drop file
            pop     bc                  ; drop buf
            jp      fat_ret_clean4$

_fat_read::
            ld      a,#FATREQ_OP_READ
            jp      fat_file_submit$

_fat_write::
            ld      a,#FATREQ_OP_WRITE
            jp      fat_file_submit$

            ;; ----------------------------------------------------------------
            ;; fat_handle_read$ / fat_handle_write$
            ;; ----------------------------------------------------------------
fat_handle_read$:
            xor     a
            jr      fat_handle_io$

fat_handle_write$:
            ld      a,#1

fat_handle_io$:
            ld      (fat_file_is_write$),a

            call    fat_decode_req_evfdev$
            call    fat_load_de$
            ld      (fat_file_buf$),de

            inc     hl                  ; req->bytes low
            ld      c,(hl)
            inc     hl                  ; req->bytes high
            ld      b,(hl)
            inc     hl                  ; req->arg
            call    fat_load_de$
            ld      a,d
            or      e
            jp      z,fhio_bad$
            ld      (fat_file_ptr$),de

            ld      a,c
            or      a
            jr      nz,fhio_enotsup$
fhio_bytes_ok$:
            ld      a,b
            ld      (fat_file_secs_left$),a
            or      a
            jr      nz,fhio_have_count$
            ld      hl,#FAT_OK
            jp      fhio_finish_hl$

fhio_have_count$:
            ld      hl,(fat_work_fs$)
            ld      a,h
            or      l
            jp      z,fhio_bad$
            ld      de,(fat_work_dev$)
            ld      a,d
            or      e
            jr      z,fhio_ebadf_mid$

            ld      bc,#FATFS_MOUNTED
            call    fat_fs_byte$
            or      a
            jr      z,fhio_ebadf_mid$

            ld      hl,(fat_file_ptr$)
            ld      bc,#FATFILE_ATTR
            add     hl,bc
            ld      a,(hl)
            ld      b,a
            and     #FAT_ATTR_DIRECTORY
            jr      z,fhio_file_ok$
fhio_ebadf_mid$:
            ld      hl,#FAT_EBADF
            jp      fhio_finish_hl$
fhio_file_ok$:
            ld      a,(fat_file_is_write$)
            or      a
            jr      z,fhio_mode_ok$
            ld      a,b
            and     #FAT_ATTR_READ_ONLY
            jr      nz,fhio_ebadf_mid$
fhio_mode_ok$:

            ld      hl,(fat_file_ptr$)
            ld      bc,#FATFILE_SIZE
            add     hl,bc
            ld      a,(hl)
            or      a
            jr      z,fhio_size_lo_ok$
fhio_enotsup$:
            ld      hl,#FAT_ENOTSUP
            jp      fhio_finish_hl$
fhio_size_lo_ok$:
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      (fat_file_size_secs$),de
            inc     hl
            ld      a,(hl)
            or      a
            jr      nz,fhio_enotsup$
fhio_size_hi_ok$:

            ld      hl,(fat_file_ptr$)
            ld      bc,#FATFILE_POS
            add     hl,bc
            ld      a,(hl)
            or      a
            jr      nz,fhio_enotsup$
fhio_pos_lo_ok$:
            inc     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ld      (fat_file_pos_secs$),de
            inc     hl
            ld      a,(hl)
            or      a
            jr      nz,fhio_enotsup$
fhio_pos_hi_ok$:

            ld      hl,(fat_file_pos_secs$)
            ld      de,(fat_file_size_secs$)
            or      a
            sbc     hl,de
            jr      c,fhio_pos_ok$
            jr      z,fhio_pos_ok$
            jr      fhio_efbig$
fhio_pos_ok$:

            ld      hl,(fat_file_pos_secs$)
            ld      a,(fat_file_secs_left$)
            ld      e,a
            ld      d,#0
            add     hl,de
            ld      (fat_file_end_secs$),hl
            jr      nc,fhio_end_add_ok$
            jr      fhio_efbig$
fhio_end_add_ok$:

            ld      a,(fat_file_is_write$)
            or      a
            jr      nz,fhio_in_range$
            ld      hl,(fat_file_size_secs$)
            ld      de,(fat_file_end_secs$)
            or      a
            sbc     hl,de
            jr      nc,fhio_in_range$
fhio_efbig$:
            ld      hl,#FAT_EFBIG
            jr      fhio_err_finish$
fhio_in_range$:

            ld      bc,#FATFS_SECTORS_PER_CL
            call    fat_fs_byte$
            ld      (fat_file_spc$),a
            call    fat_spc_shift$
            jr      nc,fhio_shift_ok$
fhio_corrupt_mid$:
            ld      hl,#FAT_ECORRUPT
fhio_err_finish$:
            jp      fhio_finish_hl$
fhio_shift_ok$:
            ld      (fat_file_shift$),a
            xor     a
            ld      (fat_file_at_boundary$),a

            ld      a,(fat_file_is_write$)
            or      a
            jr      z,fhio_locate$
            ld      hl,(fat_file_pos_secs$)
            ld      de,(fat_file_size_secs$)
            or      a
            sbc     hl,de
            jr      nz,fhio_locate$
            ld      a,h
            or      l
            jr      z,fhio_locate$
            ld      hl,(fat_file_pos_secs$)
            ld      a,(fat_file_spc$)
            dec     a
            and     l
            jr      nz,fhio_locate$
            ld      hl,(fat_file_pos_secs$)
            dec     hl
            ld      (fat_file_pos_secs$),hl
            ld      a,#1
            ld      (fat_file_at_boundary$),a

fhio_locate$:
            call    fat_locate_cluster$
            push    hl
            ld      a,(fat_file_at_boundary$)
            or      a
            jr      z,fhio_locate_restored$
            ld      hl,(fat_file_pos_secs$)
            inc     hl
            ld      (fat_file_pos_secs$),hl
fhio_locate_restored$:
            pop     hl
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
            ld      a,(fat_file_at_boundary$)
            or      a
            jr      z,fhio_loop$
            ld      a,(fat_file_spc$)
            ld      (fat_file_sec_in_cluster$),a

fhio_loop$:
            ld      a,(fat_file_secs_left$)
            or      a
            jp      z,fhio_done$

            ld      a,(fat_file_sec_in_cluster$)
            ld      b,a
            ld      a,(fat_file_spc$)
            cp      b
            jr      nz,fhio_cluster_slot$
            xor     a
            ld      (fat_file_sec_in_cluster$),a
            ld      de,(fat_file_cluster$)
            ld      a,(fat_file_is_write$)
            or      a
            jr      z,fhio_need_next$
            call    fat_get_fat_entry$
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
            push    de
            call    fat_is_eoc$
            pop     de
            jr      z,fhio_step_set$
            ld      de,(fat_file_cluster$)
            call    fat_alloc_chain_cluster$
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
fhio_step_set$:
            ld      (fat_file_cluster$),de
            jr      fhio_cluster_slot$

fhio_need_next$:
            call    fat_get_fat_entry$
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
            push    de
            call    fat_is_eoc$
            pop     de
            jr      z,fhio_step_set$
            ld      hl,#FAT_ECORRUPT
            jp      fhio_finish_hl$

fhio_fail_hl$:
            jp      fhio_finish_hl$
fhio_cluster_slot$:
            ld      de,(fat_file_cluster$)
            ld      a,d
            or      e
            jr      nz,fhio_have_cluster$
            ld      hl,(fat_file_size_secs$)
            ld      a,h
            or      l
            jr      nz,fhio_corrupt$
            ld      a,(fat_file_is_write$)
            or      a
            jr      z,fhio_corrupt$
            ld      de,#0x0000
            call    fat_alloc_chain_cluster$
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
            ld      (fat_file_cluster$),de
            push    de
            ld      hl,(fat_file_ptr$)
            call    fat_store_file_first_cluster$
            ld      hl,(fat_file_ptr$)
            call    fat_update_file_dirent$
            pop     de
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
            jr      fhio_have_cluster$
fhio_corrupt$:
            jp      fhio_corrupt_mid$
fhio_have_cluster$:

            call    fat_cluster_to_sector$
            ld      a,h
            or      l
            jr      nz,fhio_fail_hl$
            ld      a,(fat_file_sec_in_cluster$)
            ld      l,a
            ld      h,#0
            add     hl,de
            ex      de,hl               ; de = rel sector

            ld      a,(fat_file_is_write$)
            or      a
            jr      z,fhio_read_sector$
            ld      hl,(fat_file_buf$)
            ld      de,#fat_sector$
            ld      bc,#FAT_SECTOR_SIZE
            ldir
            ld      (fat_file_buf$),hl
            call    fat_write_volume_sector$
            ld      a,h
            or      l
            jr      nz,fhio_finish_hl$
            jr      fhio_after_sector$

fhio_read_sector$:
            call    fat_read_volume_sector$
            ld      a,h
            or      l
            jr      nz,fhio_finish_hl$
            ld      de,(fat_file_buf$)
            ld      hl,#fat_sector$
            ld      bc,#FAT_SECTOR_SIZE
            ldir
            ld      (fat_file_buf$),de

fhio_after_sector$:
            ld      hl,(fat_file_pos_secs$)
            inc     hl
            ld      (fat_file_pos_secs$),hl
            ld      a,(fat_file_secs_left$)
            dec     a
            ld      (fat_file_secs_left$),a
            ld      a,(fat_file_sec_in_cluster$)
            inc     a
            ld      (fat_file_sec_in_cluster$),a
            jp      fhio_loop$

fhio_done$:
            call    fat_store_file_pos$
            ld      a,(fat_file_is_write$)
            or      a
            jr      z,fhio_done_ok$
            ld      hl,(fat_file_size_secs$)
            ld      de,(fat_file_end_secs$)
            or      a
            sbc     hl,de
            jr      nc,fhio_done_ok$
            ld      hl,(fat_file_ptr$)
            ld      de,(fat_file_end_secs$)
            call    fat_store_file_size$
            ld      hl,(fat_file_ptr$)
            call    fat_update_file_dirent$
            ld      a,h
            or      l
            jr      nz,fhio_finish_hl$
fhio_done_ok$:
            ld      hl,#FAT_OK
            jr      fhio_finish_hl$

fhio_bad$:
            ld      hl,#FAT_EINVAL
fhio_finish_hl$:
            ex      de,hl
            ld      hl,(fat_file_ptr$)
            ld      bc,(fat_work_event$)
            jp      fat_complete_dirent$

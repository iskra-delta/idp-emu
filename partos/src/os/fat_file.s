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
            .globl  fat_decode_req_evfdev$
            .globl  fat_prepare_dirent_busy$
            .globl  fat_queue_req$
            .globl  fat_queue_push$
            .globl  fat_req_base$
            .globl  fat_submit_path_req$
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
            ld      bc,#FATFILE_FIRST_CLUSTER
            add     hl,bc
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
            ld      bc,#FATFILE_FIRST_CLUSTER
            add     hl,bc
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

            push    bc                  ; keep op/flags while we prepare file
            push    de                  ; keep path while we prepare the file
            push    hl                  ; keep fs while we prepare the file
            ld      hl,#8
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked file pointer
            pop     de                  ; de = fs
            push    de                  ; keep fs for later restore
            ld      h,b
            ld      l,c
            push    hl
            ld      bc,#FATFILE_FS
            add     hl,bc
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            pop     hl
            call    fat_prepare_dirent_busy$
            pop     hl                  ; restore fs
            pop     de                  ; restore path
            pop     bc                  ; restore packed op/flags

            push    de                  ; save path
            push    hl                  ; save fs
            ld      d,b
            ld      e,c
            jp      fat_submit_path_req$

            ;; ----------------------------------------------------------------
            ;; fat_file_submit$(<a> op, <hl> file, <de> buf, <stack> bytes,event)
            ;; ----------------------------------------------------------------
fat_file_submit$:
            push    af
            ld      a,h
            or      l
            jr      z,ffs_invalid$
            ld      a,d
            or      e
            jr      nz,ffs_valid$

ffs_invalid$:
            pop     bc
            jp      fat_ret_einval4$

ffs_valid$:
            push    de
            push    hl
            ld      hl,#10
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            call    fat_evt_reset$
            pop     hl
            pop     de

            push    hl
            ld      bc,#FATFILE_STATUS
            add     hl,bc
            ld      (hl),#<FAT_EBUSY
            inc     hl
            ld      (hl),#>FAT_EBUSY
            pop     hl

            push    de
            push    hl
            call    _fat_init
            pop     hl
            pop     de
            ld      a,d
            or      e
            jr      z,ffs_inited$
            jr      ffs_complete_de_pop$

ffs_inited$:
            push    de
            push    hl
            call    fat_alloc_req$
            pop     hl
            pop     de

            jr      nz,ffs_have_req$
            ld      de,#FAT_ENOMEM
            jr      ffs_complete_de_pop$

ffs_have_req$:
            pop     af                  ; restore op, drop saved af
            push    de                  ; save buf
            push    hl                  ; save file
            ld      b,a                 ; save op in b
            call    fat_req_base$
            ld      a,b
            ld      (hl),a              ; op
            inc     hl
            xor     a
            ld      (hl),a              ; flags = 0
            inc     hl

            push    hl
            ld      hl,#10
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked event
            pop     hl
            ld      (hl),c
            inc     hl
            ld      (hl),b
            inc     hl

            push    hl
            ld      hl,#2
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = saved file
            pop     hl
            push    hl
            ex      de,hl               ; hl = file
            ld      bc,#FATFILE_FS
            add     hl,bc
            call    fat_load_de$        ; de = file->fs
            pop     hl
            call    fat_store_de$

            push    hl
            ex      de,hl               ; hl = fs
            ld      bc,#FATFS_DEV
            add     hl,bc
            call    fat_load_de$        ; de = fs->dev
            pop     hl
            call    fat_store_de$

            push    hl
            ld      hl,#4
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = saved buf
            pop     hl
            call    fat_store_de$

            push    hl
            ld      hl,#8
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = stacked bytes
            pop     hl
            call    fat_store_de$

            push    hl
            ld      hl,#2
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = saved file
            pop     hl
            call    fat_store_de$       ; arg = file

            pop     bc                  ; discard saved file
            pop     bc                  ; discard saved buf

            call    fat_queue_req$
            jp      fat_ret_clean4$

ffs_complete_de_pop$:
            pop     bc                  ; discard saved af
            push    hl
            ld      hl,#6
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked event
            pop     hl                  ; hl = file
            call    fat_complete_dirent$
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

            ld      hl,(fat_work_fs$)
            ld      bc,#FATFS_MOUNTED
            add     hl,bc
            ld      a,(hl)
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

            ld      hl,(fat_work_fs$)
            ld      bc,#FATFS_SECTORS_PER_CL
            add     hl,bc
            ld      a,(hl)
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

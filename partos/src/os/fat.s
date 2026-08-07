            ;; fat.s
            ;;
            ;; shared FAT12/FAT16 worker core for PartOS.
            ;;
            ;; this file is the "spine" of the FAT service. it owns:
            ;;   - shared helper routines used by all FAT modules
            ;;   - the single request queue and its synchronization events
            ;;   - the dedicated worker thread that serializes device access
            ;;   - all shared scratch/sysvar storage used by worker-side code
            ;;
            ;; mount and lookup logic live in their own files, but they both
            ;; depend on the worker, queue and shared sector buffer defined
            ;; here. keeping those common pieces together makes the split much
            ;; easier to navigate without duplicating storage or helper code.
            ;;
            ;; goals of this first assembly cut:
            ;;   - tiny public API: init + async mount-by-name / mount-by-dev
            ;;     plus async read-only path lookup
            ;;   - request queue made of sysobj-shaped nodes from day one
            ;;   - one worker thread that serializes device access
            ;;   - support both superfloppy boot sectors and hard-disk MBRs
            ;;   - fill one fat_fs_t with enough metadata for later file work
            ;;
            ;; important design notes:
            ;;   - requests start with a next/owner pair so future process
            ;;     teardown can unlink every pending FAT request belonging to a
            ;;     dead owner.
            ;;   - the queue nodes themselves live in the shared user heap
            ;;     under owner NONE. req->owner tracks the logical caller
            ;;     owner, while block lifetime is controlled explicitly by the
            ;;     FAT worker/reaper path so an in-flight request never
            ;;     disappears under it and the tiny sys-heap is not exhausted
            ;;     by repeated directory scans.
            ;;   - the worker is the ONLY code path that touches the shared FAT
            ;;     sector buffer, so a single 256-byte staging block is enough.
            ;;   - all worker scratch lives in _SYSVARS, which keeps stack
            ;;     usage tiny and makes cross-module helper calls predictable.
            ;;   - the MBR lives across two 256-byte device blocks. we first
            ;;     try block 0 as a native FAT boot sector; only if that fails
            ;;     do we read block 1 and inspect the PC-style partition table.
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module fat

            .include "../drivers/dev.inc"
            .include "../drivers/drv.inc"
            .include "fat.inc"

            .globl  _fat_init
            .globl  _fat_mount_dev
            .globl  _fat_mount
            .globl  _fat_lookup
            .globl  _fat_open
            .globl  _fat_create
            .globl  _fat_read
            .globl  _fat_write
            .globl  _fat_purge_owner
            .globl  fat_handle_mount$
            .globl  fat_handle_lookup$
            .globl  fat_handle_readdir$
            .globl  fat_handle_create$
            .globl  fat_handle_read$
            .globl  fat_handle_write$
            .globl  fat_handle_unlink$
            .globl  fat_handle_mkdir$
            .globl  fat_handle_rmdir$

            .globl  fat_ret_clean2$
            .globl  fat_ret_clean4$
            .globl  fat_ret_einval4$
            .globl  fat_complete_stacked_dirent4$
            .globl  fat_evt_reset$
            .globl  fat_complete_fs$
            .globl  fat_complete_dirent$
            .globl  fat_finish_dirent$
            .globl  fat_store_de$
            .globl  fat_load_de$
            .globl  fat_fs_de$
            .globl  fat_fs_byte$
            .globl  fat_dirent_de$
            .globl  fat_dirent_byte$
            .globl  fat_decode_req_evfdev$
            .globl  fat_alloc_req$
            .globl  fat_prepare_fs_busy$
            .globl  fat_prepare_dirent_busy$
            .globl  fat_req_base$
            .globl  fat_path_submit_check$
            .globl  fat_submit_path_req$
            .globl  fat_fill_root_dirent$
            .globl  fat_upper$
            .globl  fat_namechar_ok$
            .globl  fat_skip_slashes$
            .globl  fat_dev_open$
            .globl  fat_read_block$
            .globl  fat_read_block_to$
            .globl  fat_write_block$
            .globl  fat_cache_fdev$
            .globl  fat_cache_fsec$
            .globl  fat_xfer_buf$
            .globl  fat_write_volume_sector$
            .globl  fat_queue_req$
            .globl  fat_queue_push$

            .globl  fat_queue_event$
            .globl  fat_io_event$
            .globl  fi_busy$
            .globl  fi_nomem$
            .globl  fi_fail_queue_evt$
            .globl  fi_fail_both_evts$
            .globl  fi_after_guard_alloc$
            .globl  fat_worker_loop$
            .globl  fat_work_fs$
            .globl  fat_work_dev$
            .globl  fat_work_event$
            .globl  fat_submit_event$
            .globl  fat_submit_frame$

            .globl  fat_req_flags$
            .globl  fat_candidate_base$
            .globl  fat_cluster_shift$
            .globl  fat_spc_raw$
            .globl  fat_mbr_unsupported$
            .globl  fat_tmp_num_fats$
            .globl  fat_tmp_reserved$
            .globl  fat_tmp_root_entries$
            .globl  fat_tmp_spf$
            .globl  fat_tmp_total$
            .globl  fat_tmp_root_dir_sectors$
            .globl  fat_tmp_root_start$
            .globl  fat_tmp_data_start$
            .globl  fat_tmp_clusters$
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
            .globl  fat_create_entry$
            .globl  fat_create_cluster$

            .globl  _evt_create
            .globl  _evt_destroy
            .globl  sched_wake_now$
            .globl  _evt_set
            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_wait4events
            .globl  _thread_current
            .globl  __find_dev_drv
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  _owner_cleanup_register
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  __sys_heap
            .globl  __usr_heap
            .globl  drv_owner_current

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; helper: return from a 3-argument sdcccall(1) function where the
            ;; third argument was a stacked 16-bit pointer. de already holds the
            ;; return value.
            ;; ----------------------------------------------------------------
fat_ret_clean2$:
            pop     hl                  ; return address
            pop     bc                  ; drop the stacked event pointer
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; helper: return from a 4-argument sdcccall(1) function where the
            ;; last two arguments were stacked 16-bit pointers.
            ;; ----------------------------------------------------------------
fat_ret_clean4$:
            pop     hl                  ; return address
            pop     bc                  ; drop stacked arg #3
            pop     bc                  ; drop stacked arg #4
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; helper: complete one stacked dirent/file result whose pointer is
            ;; the first stacked 16-bit arg and whose event is the second. this
            ;; matches fat_lookup() and fat_open()/fat_create() submit frames.
            ;; ----------------------------------------------------------------
fat_complete_stacked_dirent4$:
            ld      hl,#4
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked event
            ld      hl,#2
            add     hl,sp
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a                 ; hl = stacked dirent/file
            call    fat_complete_dirent$
            jr      fat_ret_clean4$

            ;; ----------------------------------------------------------------
            ;; helper: return FAT_EINVAL from a clean4 sdcc call frame.
            ;; ----------------------------------------------------------------
fat_ret_einval4$:
            ld      de,#FAT_EINVAL
            jr      fat_ret_clean4$

            ;; ----------------------------------------------------------------
            ;; fat_evt_state$(<hl> event, <a> new_state)
            ;; ----------------------------------------------------------------
            ;; small null-safe wrapper around evt_set. we use it for both the
            ;; caller's completion event and the worker's private queue/i/o
            ;; events.
            ;; ----------------------------------------------------------------
fat_evt_state$:
            ld      b,a
            ld      a,h
            or      l
            ret     z
            ld      a,b
            or      a
            jr      z,fes_evt$          ; reset (NONSIGNALED): no fast-wake
            ;; disk/FAT handoff (worker<->process, queue/completion): resume the
            ;; woken thread on the next reschedule instead of a VBL frame later.
            ;; Keyboard/SIO does NOT route through here, so console timing is
            ;; unchanged. Consumed by __thread_select_next.
            push    af
            ld      a,#1
            ld      (sched_wake_now$),a
            pop     af
fes_evt$:
            push    af
            inc     sp                 ; leave A (new state) as the 1-byte arg
            call    _evt_set            ; must CALL (evt_set reads arg above its
            ret                         ; return addr); a tail jp misaligns both

fat_evt_reset$:
            ld      a,#FAT_NONSIGNALED
            jr      fat_evt_state$

            ;; ----------------------------------------------------------------
            ;; fat_complete_fs$(<hl> fs, <bc> event, <de> status)
            ;; fat_complete_dirent$(<hl> dirent, <bc> event, <de> status)
            ;; ----------------------------------------------------------------
            ;; both async completion paths share the same "store status, then
            ;; optionally signal the caller's event" tail. only the status-field
            ;; offset differs.
            ;; ----------------------------------------------------------------
fat_complete_fs$:
            ld      a,#FATFS_STATUS
            jr      fat_complete_obj$

fat_complete_dirent$:
            ld      a,#FATDIRENT_STATUS

fat_complete_obj$:
            push    bc
            ld      c,a
            ld      b,#0
            add     hl,bc
            pop     bc
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ld      a,b
            or      c
            ret     z
            push    bc
            pop     hl
            ld      a,#FAT_SIGNALED
            call    fat_evt_state$
            ret

            ;; ----------------------------------------------------------------
            ;; fat_finish_dirent$(<de> status)
            ;; ----------------------------------------------------------------
            ;; shared worker-side completion tail: store status into the current
            ;; lookup result and signal its event. every path/dir handler ends
            ;; here, so folding the three-instruction tail saves bytes per site.
            ;; ----------------------------------------------------------------
fat_finish_dirent$:
            ld      hl,(fat_lookup_dirent$)
            ld      bc,(fat_work_event$)
            jp      fat_complete_dirent$

            ;; ----------------------------------------------------------------
            ;; fat_store_de$(<hl> addr, <de> value)
            ;; fat_load_de$(<hl> addr) -> <de> value
            ;; ----------------------------------------------------------------
            ;; fat_store_de$ advances hl past the stored 16-bit word so callers
            ;; can chain sequential stores without a second helper.
fat_store_de$:
            ld      (hl),e
            inc     hl
            ld      (hl),d
            inc     hl
            ret

fat_load_de$:
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ret

            ;; ----------------------------------------------------------------
            ;; fat_fs_de$(<bc> offset)   -> <de> fs-field word, <hl> = fs+off+1
            ;; fat_fs_byte$(<bc> offset)  -> <a>  fs-field byte, <hl> = fs+off
            ;; ----------------------------------------------------------------
            ;; shared accessors for the mounted-volume descriptor. callers used
            ;; to open-code ld hl,(fat_work_fs$) / add hl,bc / load on every
            ;; field; folding that here reclaims several bytes per call site.
            ;; ----------------------------------------------------------------
fat_fs_de$:
            ld      hl,(fat_work_fs$)
            add     hl,bc
            jr      fat_load_de$
fat_fs_byte$:
            ld      hl,(fat_work_fs$)
            add     hl,bc
            ld      a,(hl)
            ret

            ;; ----------------------------------------------------------------
            ;; fat_dirent_de$(<bc> offset)  -> <de> field, <hl> = dirent+off+1
            ;; fat_dirent_byte$(<bc> offset) -> <a>  field, <hl> = dirent+off
            ;; ----------------------------------------------------------------
            ;; same idea as fat_fs_de$/byte$ for the current lookup result.
            ;; ----------------------------------------------------------------
fat_dirent_de$:
            ld      hl,(fat_lookup_dirent$)
            add     hl,bc
            jr      fat_load_de$
fat_dirent_byte$:
            ld      hl,(fat_lookup_dirent$)
            add     hl,bc
            ld      a,(hl)
            ret

            ;; ----------------------------------------------------------------
            ;; fat_decode_req_evfdev$(<de> req) -> <hl> req->buf
            ;; ----------------------------------------------------------------
            ;; decodes the common queued-request prefix shared by mount, path
            ;; and file-data workers:
            ;;   req->flags
            ;;   req->event
            ;;   req->fs
            ;;   req->dev
            ;; and stores the decoded values into the shared worker scratch
            ;; slots. lookup/create reuse fat_req_flags$ as their 1-byte
            ;; "file-only" request scratch.
            ;; hl returns positioned at req->buf so callers can continue a
            ;; linear decode of the request-specific tail.
            ;; ----------------------------------------------------------------
fat_decode_req_evfdev$:
            push    de
            pop     hl
            ld      bc,#FATREQ_FLAGS
            add     hl,bc
            ld      a,(hl)
            ld      (fat_req_flags$),a
            inc     hl                  ; req->event
            call    fat_load_de$
            ld      (fat_work_event$),de

            inc     hl                  ; req->fs
            call    fat_load_de$
            ld      (fat_work_fs$),de

            inc     hl                  ; req->dev
            call    fat_load_de$
            ld      (fat_work_dev$),de

            inc     hl                  ; hl = req->buf
            ret

            ;; ----------------------------------------------------------------
            ;; fat_alloc_req$() -> <ix> req
            ;; ----------------------------------------------------------------
            ;; allocates one ownerless FAT request block from the shared user
            ;; heap. returns ix=0 on failure, matching the old open-coded path.
            ;; Z is also set on failure so callers can branch immediately after
            ;; restoring any saved registers without re-testing ix manually.
            ;; ----------------------------------------------------------------
fat_alloc_req$:
            ld      de,#0x0000
            push    de                  ; owner NONE: freed explicitly later
            ld      de,#FATREQ_SIZE
            ld      hl,#__usr_heap
            call    _mem_allocate
            pop     bc                  ; discard NONE owner
            push    de
            pop     ix                  ; ix = request payload
            ld      a,d
            or      e
            ret

            ;; ----------------------------------------------------------------
            ;; fat_queue_req$(<ix> req) -> <de> FAT_OK
            ;; ----------------------------------------------------------------
            ;; stamps the logical owner into req->owner, queues the request and
            ;; returns FAT_OK in the normal sdcc de return register.
            ;; ----------------------------------------------------------------
fat_queue_req$:
            call    drv_owner_current
            push    ix
            pop     hl
            inc     hl
            inc     hl                  ; req->owner
            call    fat_store_de$
            push    ix
            pop     de
            call    fat_queue_push$
            ld      de,#FAT_OK
            ret

            ;; ----------------------------------------------------------------
            ;; fat_req_base$(<ix> req) -> <hl> req->op, <a> 0
            ;; ----------------------------------------------------------------
            ;; initializes the sysobj-style request header prefix:
            ;;   req->next = 0
            ;;   skip req->owner for later fill
            ;; and leaves hl positioned at req->op so callers can keep their
            ;; operation-specific packing inline.
            ;; ----------------------------------------------------------------
fat_req_base$:
            push    ix
            pop     hl
            xor     a
            ld      (hl),a              ; req->next lo = 0
            inc     hl
            ld      (hl),a              ; req->next hi = 0
            inc     hl                  ; hl = req->owner (left for fat_queue_req$)
            inc     hl
            inc     hl                  ; hl = req->op (offset FATREQ_OP = 4)
            ret

            ;; ----------------------------------------------------------------
            ;; fat_path_submit_check$(<hl> fs,<de> path) -> <hl>/<de> preserved
            ;; ----------------------------------------------------------------
            ;; shared precheck for path-based clean4 submissions. it validates:
            ;;   - fs != 0
            ;;   - path != 0
            ;;   - stacked dirent/file argument != 0
            ;; and resets the stacked completion event. on invalid input it
            ;; drops its own return address and exits through fat_ret_einval4$.
            ;; ----------------------------------------------------------------
fat_path_submit_check$:
            ;; sdcc clean4 frame below this helper's own return address:
            ;;   [sp+0]=check_ret [sp+2]=caller_ret [sp+4]=event [sp+6]=file
            ;; hl=fs, de=path
            ld      a,h
            or      l
            jr      z,fpsc_bad$
            ld      a,d
            or      e
            jr      z,fpsc_bad$

            push    de                  ; save path
            push    hl                  ; save fs
            push    bc                  ; save caller op/flags

            ld      hl,#12
            add     hl,sp
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,fpsc_bad_pop$

            ld      hl,#10
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            push    bc
            pop     hl                  ; hl = stacked event pointer
            call    fat_evt_reset$

            pop     bc                  ; restore caller op/flags
            pop     hl                  ; restore fs
            pop     de                  ; restore path
            ret

fpsc_bad_pop$:
            pop     bc
            pop     hl
            pop     de
fpsc_bad$:
            pop     bc                  ; drop our own return address first
            jp      fat_ret_einval4$

            ;; ----------------------------------------------------------------
            ;; fat_submit_path_req$(<d> op,<e> flags) -> no return
            ;; ----------------------------------------------------------------
            ;; shared tail for path-based submissions. callers leave fs/path in
            ;; fat_submit_frame$ and keep the sdcc clean4 stack:
            ;;   [sp+0] caller return address
            ;;   [sp+2] stacked event pointer
            ;;   [sp+4] stacked dirent/file pointer
            ;;
            ;; de carries the packed submit opcode/flags. this helper now also
            ;; absorbs the old "ready" stage: it runs fat_init, allocates one
            ;; request block, then finishes packing and queueing the request.
            ;; ----------------------------------------------------------------
fat_submit_path_req$:
            ld      (fat_submit_op_flags$),de
            ld      hl,#2
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = stacked event
            ld      (fat_submit_path_event$),de
            ld      hl,#4
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = stacked dirent/file
            ld      (fat_submit_event$),de
            ld      hl,#fat_submit_op_flags$ + 1
            ld      a,(hl)              ; packed op byte (de high on little-endian)
            cp      #FATREQ_OP_MOUNT
            jr      c,fsptr_einval_op$
            cp      #FATREQ_OP_RMDIR + 1
            jr      nc,fsptr_einval_op$
            call    fsptr_validate_frame$
            ld      a,h
            or      l
            jr      nz,fsptr_einval_op$
            call    _fat_init
            ld      a,d
            or      e
            jr      z,fsptr_alloc$
            jr      fsptr_fail_de$

fsptr_alloc$:
            call    fat_alloc_req$
            jr      nz,fsptr_pack$
            ld      de,#FAT_ENOMEM
            jr      fsptr_fail_de$

fsptr_einval_op$:
            ld      de,#FAT_EINVAL
            jr      fsptr_fail_de$

            ;; ----------------------------------------------------------------
            ;; fsptr_validate_frame$() -> <hl> FAT_OK | error
            ;; ----------------------------------------------------------------
            ;; rejects path submissions whose caller frame lost fs/path or
            ;; points at a volume that is not mounted+ready.
            ;; ----------------------------------------------------------------
fsptr_validate_frame$:
            ld      de,(fat_submit_frame$)
            ld      a,d
            or      e
            jr      z,fvf_fail$
            push    de
            pop     hl
            ld      bc,#FATFS_MOUNTED
            add     hl,bc
            ld      a,(hl)
            dec     a
            jr      nz,fvf_fail$
            inc     hl
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      nz,fvf_fail$
            ld      de,(fat_submit_frame$ + 2)
            ld      a,d
            or      e
            jr      z,fvf_fail$
            ld      hl,#FAT_OK
            ret
fvf_fail$:
            ld      hl,#FAT_EINVAL
            ret

fsptr_pack$:
            ld      de,(fat_submit_op_flags$)
            call    fat_req_base$
            ld      a,d
            ld      (hl),a
            inc     hl
            ld      a,e
            ld      (hl),a
            inc     hl
            ld      de,(fat_submit_path_event$)
            call    fat_store_de$       ; stacked event
            ld      de,(fat_submit_frame$)
            call    fat_store_de$
            push    hl
            ld      de,(fat_submit_frame$)
            ex      de,hl
            ld      bc,#FATFS_DEV
            add     hl,bc
            call    fat_load_de$
            pop     hl
            call    fat_store_de$
            ld      de,(fat_submit_frame$ + 2)
            call    fat_store_de$
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      de,(fat_submit_frame$ + 6)
            call    fat_store_de$       ; frame+6 = stacked dirent/file
            call    fat_queue_req$
            jp      fat_ret_clean4$

fsptr_fail_de$:
            ld      hl,(fat_submit_event$)
            ld      bc,(fat_submit_path_event$)
            push    de                  ; preserve sdcc return code across completion
            call    fat_complete_dirent$
            pop     de
            jp      fat_ret_clean4$

            ;; ----------------------------------------------------------------
            ;; fat_prepare_fs_busy$(<hl> fs, <de> dev)
            ;; ----------------------------------------------------------------
            ;; makes the volume descriptor look "pending":
            ;;   - remember the target device
            ;;   - clear lba_base
            ;;   - clear mounted
            ;;   - set status to FAT_EBUSY
            ;;
            ;; later failure paths do not need to scrub every field because the
            ;; mounted flag stays cleared until fat_fill_fs_from_boot$ succeeds.
            ;; ----------------------------------------------------------------
fat_prepare_fs_busy$:
            push    hl
            call    fat_store_de$       ; fs->dev = dev
            xor     a
            ld      (hl),a              ; fs->lba_base low
            inc     hl
            ld      (hl),a              ; fs->lba_base high
            ld      bc,#(FATFS_MOUNTED - FATFS_LBA_BASE - 1)
            add     hl,bc
            ld      (hl),a              ; fs->mounted = 0
            inc     hl
            ld      de,#FAT_EBUSY
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; fs->status = FAT_EBUSY
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; fat_prepare_dirent_busy$(<hl> dirent)
            ;; ----------------------------------------------------------------
            ;; clears the lookup result payload and marks it busy so callers
            ;; never observe stale metadata from a previous successful lookup.
            ;; ----------------------------------------------------------------
fat_prepare_dirent_busy$:
            push    hl
            xor     a
            ld      b,#FATDIRENT_STATUS
fpdb_zero$:
            ld      (hl),a
            inc     hl
            djnz    fpdb_zero$
            ld      de,#FAT_EBUSY
            ld      (hl),e
            inc     hl
            ld      (hl),d
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; fat_fill_root_dirent$(<hl> dirent)
            ;; ----------------------------------------------------------------
            ;; synthetic metadata for the fixed FAT12/16 root directory. it has
            ;; no cluster chain, so first_cluster/size are zero and dir_sector
            ;; is set to 0xffff as a sentinel meaning "not backed by one real
            ;; 32-byte on-disk entry".
            ;; ----------------------------------------------------------------
fat_fill_root_dirent$:
            call    fat_prepare_dirent_busy$
            push    hl
            ld      bc,#FATDIRENT_DIR_SECTOR
            add     hl,bc
            ld      (hl),#0xff
            inc     hl
            ld      (hl),#0xff
            inc     hl
            xor     a
            ld      (hl),a              ; dir_offset = 0
            inc     hl
            ld      (hl),#FAT_ATTR_DIRECTORY
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; fat_upper$(<a> ch) -> <a> uppercased
            ;; ----------------------------------------------------------------
fat_upper$:
            cp      #'a'
            ret     c
            cp      #'z' + 1
            ret     nc
            sub     #'a' - 'A'
            ret

            ;; ----------------------------------------------------------------
            ;; fat_namechar_ok$(<a> ch)
            ;; ----------------------------------------------------------------
            ;; carry clear = accepted DOS 8.3 character, carry set = rejected.
            ;; this keeps the parser tiny and intentionally conservative.
            ;; ----------------------------------------------------------------
fat_namechar_ok$:
            cp      #'A'
            jr      c,fnc_digit$
            cp      #'Z' + 1
            jr      c,fnc_ok$
fnc_digit$:
            cp      #'0'
            jr      c,fnc_punct$
            cp      #'9' + 1
            jr      c,fnc_ok$
fnc_punct$:
            cp      #'_'
            jr      z,fnc_ok$
            cp      #'-'
            jr      z,fnc_ok$
            cp      #'$'
            jr      z,fnc_ok$
            cp      #'~'
            jr      z,fnc_ok$
            scf
            ret
fnc_ok$:
            or      a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_skip_slashes$(<de> ptr) -> <de> ptr
            ;; ----------------------------------------------------------------
fat_skip_slashes$:
fsslash_loop$:
            ld      a,(de)
            cp      #FAT_SLASH
            ret     nz
            inc     de
            jr      fsslash_loop$

            ;; ----------------------------------------------------------------
            ;; fat_dev_open$(<hl> dev) -> <hl> rc
            ;; fat_dev_ioctl$(<hl> dev, <de> params, <bc> cmd) -> <hl> rc
            ;; fat_dev_read$(<hl> dev, <de> buf, <bc> count, <ix> event) -> rc
            ;;
            ;; all three wrappers load the real driver function pointer from
            ;; dev->driver and tail-call it. they return DRV_ERR if the device
            ;; pointer is incomplete.
            ;; ----------------------------------------------------------------
fat_dev_open$:
            push    hl
            ld      bc,#DEV_DRIVER
            add     hl,bc
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = dev->driver
            pop     hl                  ; restore dev argument
            ld      a,b
            or      c
            jr      z,fdo_fail$
            push    hl
            ld      h,b
            ld      l,c
            ld      bc,#DRV_OPEN
            add     hl,bc
            call    fat_load_de$
            pop     hl                  ; restore dev for the real open()
            ld      a,d
            or      e
            jr      z,fdo_fail$
            push    de
            ret
fdo_fail$:
            ld      hl,#DRV_ERR
            ret

fat_dev_ioctl$:
            ld      a,#DRV_IOCTL
            jr      fat_dev_call3$

fat_dev_read$:
            ld      a,#DRV_READ
            jr      fat_dev_call3$

fat_dev_write$:
            ld      a,#DRV_WRITE
            jr      fat_dev_call3$

fat_dev_call3$:
            push    bc                  ; save arg #3
            push    de                  ; save arg #2 (must stay intact: ld d,#off
            push    hl                  ; would clobber the buf pointer high byte)
            push    af                  ; save vtable offset from a
            ld      bc,#DEV_DRIVER
            add     hl,bc
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = dev->driver
            ld      a,b
            or      c
            jr      z,fdc_fail_pop$
            ld      h,b
            ld      l,c                 ; hl = dev->driver
            pop     af                  ; restore vtable offset
            ld      b,#0
            ld      c,a
            add     hl,bc
            call    fat_load_de$        ; de = real driver function
            ld      a,d
            or      e
            jr      z,fdc_fail_pop$
            push    de
            pop     iy
            pop     hl                  ; restore dev
            pop     de                  ; restore arg #2
            pop     bc                  ; restore arg #3
            push    iy
            ret
fdc_fail_pop$:
            pop     af
            pop     hl
            pop     de
            pop     bc
            ld      hl,#DRV_ERR
            ret

            ;; ----------------------------------------------------------------
            ;; fat_wait_one$(<hl> event)
            ;; ----------------------------------------------------------------
            ;; blocks the current thread on one event. thread_wait4events has an
            ;; important fast path: if the event is already signaled, it returns
            ;; immediately and we pay almost nothing for fake-async drivers.
            ;; ----------------------------------------------------------------
fat_wait_one$:
            ;; stable one-element wait array: nested call cannot clobber the
            ;; event pointer or the fat_handle_lookup$ return slot on the stack.
            ld      (fat_wait_evt$),hl
            ld      hl,#fat_wait_evt$
            ld      a,#1
            push    af
            inc     sp
            call    _ir_enable
            call    _thread_wait4events
            ret

            ;; ----------------------------------------------------------------
            ;; fat_read_block$(<hl> dev, <de> block256) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; reads one 256-byte device block into the shared FAT sector
            ;; buffer. the device layer exposes a flat 24-bit byte cursor, so a
            ;; 256-byte block index becomes [0, block_lo, block_hi].
            ;; ----------------------------------------------------------------
fat_read_block$:
            ;; default: read into the shared FAT/metadata buffer, so drop the
            ;; FAT-sector cache (fat_get_fat_entry$ re-validates after its read).
            push    hl
            ld      hl,#fat_sector$
            ld      (fat_xfer_buf$),hl
            ld      hl,#0
            ld      (fat_cache_fdev$),hl
            pop     hl
            xor     a
            jr      fat_xfer_block$

            ;; fat_read_block_to$(<hl> dev, <de> block256): read into the buffer
            ;; whose address is already in fat_xfer_buf$ (a caller/user DATA buffer).
            ;; fat_sector$ is NOT touched, so the FAT-sector cache stays valid --
            ;; this is what lets file/dir DATA reads avoid thrashing the FAT cache.
fat_read_block_to$:
            xor     a
            jr      fat_xfer_block$

fat_write_block$:
            ;; writes source fat_sector$ back; drop the FAT-sector cache too.
            push    hl
            ld      hl,#fat_sector$
            ld      (fat_xfer_buf$),hl
            ld      hl,#0
            ld      (fat_cache_fdev$),hl
            pop     hl
            ld      a,#1

fat_xfer_block$:
            push    af
            ld      a,#0
            ld      (fat_pos24$),a
            ld      a,e
            ld      (fat_pos24$ + 1),a
            ld      a,d
            ld      (fat_pos24$ + 2),a

            push    hl                  ; keep dev across the event reset
            ld      hl,(fat_io_event$)
            call    fat_evt_reset$
            pop     hl

            push    hl                  ; keep dev across ioctl
            ld      de,#fat_pos24$
            ld      bc,#IOCTL_SETPOS
            call    fat_dev_ioctl$
            ld      a,h
            or      l
            jr      nz,fxb_fail_pop$
            pop     hl                  ; restore dev for read()

            ld      de,(fat_xfer_buf$)
            ld      bc,#FAT_SECTOR_SIZE
            ld      ix,(fat_io_event$)
            pop     af
            ;; branch explicitly on the direction flag: fat_dev_read$ returns
            ;; with arbitrary flags, so a `call z,read / call nz,write` pair would
            ;; retest read's leftover flags and spuriously call write with hl=0
            ;; (a NULL device) -> fat_dev_call3$ dispatches a garbage vtable.
            or      a
            jr      nz,fxb_do_write$
            call    fat_dev_read$
            jr      fxb_io_tested$
fxb_do_write$:
            call    fat_dev_write$
fxb_io_tested$:
            ld      a,h
            or      l
            jr      nz,fxb_fail$

            ld      hl,(fat_io_event$)
            call    fat_wait_one$
            ld      hl,#FAT_OK
            ret

fxb_fail_pop$:
            pop     hl
            pop     af
fxb_fail$:
            ld      hl,#FAT_EIO
            ret

            ;; ----------------------------------------------------------------
            ;; fat_write_volume_sector$(<de> rel_sector) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; adds fs->lba_base to one volume-relative sector number and
            ;; writes fat_sector$ back through the current device.
            ;; ----------------------------------------------------------------
fat_write_volume_sector$:
            push    de
            ld      bc,#FATFS_LBA_BASE
            call    fat_fs_de$
            pop     hl
            add     hl,de
            ex      de,hl
            ld      hl,(fat_work_dev$)
            jr      fat_write_block$
            ;; ----------------------------------------------------------------
            ;; fat_queue_push$(<de> req)
            ;; fat_queue_pop$() -> <de> req or 0
            ;; ----------------------------------------------------------------
            ;; the queue is intentionally tiny:
            ;;   request pointer, owner, op, buffer, byte count, event.
            ;; no priorities, no extra bookkeeping. just fifo and one event.
            ;; ----------------------------------------------------------------
fat_queue_push$:
            call    _ir_disable
            push    de
            ex      de,hl
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a              ; req->next = 0
            pop     de                  ; de = req

            ld      hl,(fat_req_head$)
            ld      a,h
            or      l
            jr      z,fqp_empty$

            ;; A dying owner can leave a stale head behind; reject anything that
            ;; does not look like one of our request nodes before linking to it.
            push    de                  ; keep req while validating old head
            ld      bc,#FATREQ_OP
            add     hl,bc
            ld      a,(hl)
            cp      #FATREQ_OP_MOUNT
            jr      c,fqp_bad_head$
            cp      #FATREQ_OP_RMDIR + 1
            jr      nc,fqp_bad_head$
            pop     de

            ld      hl,(fat_req_tail$)
            ld      a,h
            or      l
            jr      nz,fqp_have_tail$
            ld      hl,(fat_req_head$)

fqp_have_tail$:
            call    fat_store_de$       ; old_tail->next = req
            jr      fqp_linked$
fqp_bad_head$:
            pop     de
fqp_empty$:
            ld      (fat_req_head$),de
fqp_linked$:
            ld      (fat_req_tail$),de
            ld      hl,(fat_queue_event$)
            ld      a,#FAT_SIGNALED
            call    fat_evt_state$
            call    _ir_enable
            ret

fat_queue_pop$:
            call    _ir_disable
            ld      de,(fat_req_head$)
            ld      a,d
            or      e
            jr      z,fqpop_empty$

            push    de
            ld      h,d
            ld      l,e
            ld      bc,#FATREQ_OP
            add     hl,bc
            ld      a,(hl)
            cp      #FATREQ_OP_MOUNT
            jr      c,fqpop_bad_head$
            cp      #FATREQ_OP_RMDIR + 1
            jr      nc,fqpop_bad_head$
            pop     de
            jr      fqpop_have$

fqpop_bad_head$:
            pop     de
            ld      hl,#0x0000
            ld      (fat_req_head$),hl
            ld      (fat_req_tail$),hl
            jr      fqpop_empty$

fqpop_empty$:
            ld      hl,(fat_queue_event$)
            ld      a,#FAT_NONSIGNALED
            call    fat_evt_state$
            ld      de,#0x0000
            call    _ir_enable
            ret

fqpop_have$:
            ld      (fat_pop_req$),de
            push    de
            ex      de,hl               ; hl = req
            call    fat_load_de$        ; de = req->next
            ld      (fat_req_head$),de
            pop     de                  ; drop saved req
            ld      hl,(fat_req_head$)
            ld      a,h
            or      l
            jr      nz,fqpop_done$
            ld      hl,#0x0000
            ld      (fat_req_tail$),hl
            ld      hl,(fat_queue_event$)
            ld      a,#FAT_NONSIGNALED
            call    fat_evt_state$
fqpop_done$:
            ld      de,(fat_pop_req$)
            call    _ir_enable
            ld      de,(fat_pop_req$)
            ret

            ;; ----------------------------------------------------------------
            ;; _fat_purge_owner(<de> owner)
            ;; ----------------------------------------------------------------
            ;; unlinks every still-queued FAT request owned by `owner`.
            ;;
            ;; only queued work is touched here. the worker frees its current
            ;; in-flight request on its own, but dead owners must not leave
            ;; stale FIFO nodes behind waiting to be processed later.
            ;; ----------------------------------------------------------------
_fat_purge_owner::
            ld      a,d
            or      e
            ret     z
            call    _ir_disable
            ld      (fat_purge_owner$),de
            ld      hl,(fat_req_head$)
            ld      de,#0x0000
            ld      (fat_purge_prev$),de

fpo_loop$:
            ld      a,h
            or      l
            jr      z,fpo_done$
            ld      (fat_purge_req$),hl

            call    fat_load_de$        ; de = req->next
            ld      (fat_purge_next$),de

            ld      hl,(fat_purge_req$)
            inc     hl
            inc     hl                  ; hl = req->owner
            call    fat_load_de$
            ld      hl,(fat_purge_owner$)
            ld      a,e
            cp      l
            jr      nz,fpo_keep$
            ld      a,d
            cp      h
            jr      nz,fpo_keep$

            ld      hl,(fat_purge_prev$)
            ld      a,h
            or      l
            jr      nz,fpo_unlink_prev$
            ld      de,(fat_purge_next$)
            ld      (fat_req_head$),de
            jr      fpo_unlink_tail$

fpo_unlink_prev$:
            ld      de,(fat_purge_next$)
            call    fat_store_de$       ; prev->next = next

fpo_unlink_tail$:
            ld      hl,(fat_purge_next$)
            ld      a,h
            or      l
            jr      nz,fpo_free$
            ld      hl,(fat_purge_prev$)
            ld      (fat_req_tail$),hl

fpo_free$:
            ld      hl,#__usr_heap
            ld      de,(fat_purge_req$)
            call    _mem_free
            ld      hl,(fat_purge_next$)
            jr      fpo_loop$

fpo_keep$:
            ld      de,(fat_purge_req$)
            ld      (fat_purge_prev$),de
            ld      hl,(fat_purge_next$)
            jr      fpo_loop$

fpo_done$:
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; fat_worker$()
            ;; ----------------------------------------------------------------
            ;; dedicated worker thread. it sleeps on the queue event, pops the
            ;; next request, executes it, frees the queue node and loops forever.
            ;; ----------------------------------------------------------------
fat_worker$:
fat_worker_loop$:
fw_loop$:
            call    fat_queue_pop$
            ld      a,d
            or      e
            jr      nz,fw_dispatch$
            ld      hl,(fat_queue_event$)
            call    fat_wait_one$
            jr      fw_loop$

fw_dispatch$:
            ;; Handler paths are deep and can use the stack heavily; keep the
            ;; request pointer in scratch so the common free path is stable.
            ld      (fat_current_req$),de
            ld      h,d
            ld      l,e
            ld      bc,#FATREQ_OP
            add     hl,bc
            ld      a,(hl)
            dec     a
            jr      z,fw_mount$
            dec     a
            jr      z,fw_lookup$
            dec     a
            jr      z,fw_read$
            dec     a
            jr      z,fw_write$
            dec     a
            jr      z,fw_create$
            dec     a
            jr      z,fw_readdir$
            dec     a
            jr      z,fw_unlink$
            dec     a
            jr      z,fw_mkdir$
            dec     a
            jr      z,fw_rmdir$

            ;; unknown op: fail the target fs and free the request anyway.
            call    fat_decode_req_evfdev$
            ld      de,#FAT_EINVAL
            ld      hl,(fat_work_fs$)
            ld      bc,(fat_work_event$)
            call    fat_complete_fs$
            jr      fw_after$

fw_mount$:
            call    fat_handle_mount$
            jr      fw_after$

fw_lookup$:
            call    fat_handle_lookup$
            jr      fw_after$

fw_read$:
            call    fat_handle_read$
            jr      fw_after$

fw_write$:
            call    fat_handle_write$
            jr      fw_after$

fw_create$:
            call    fat_handle_create$
            jr      fw_after$

fw_readdir$:
            call    fat_handle_readdir$
            jr      fw_after$

fw_unlink$:
            call    fat_handle_unlink$
            jr      fw_after$

fw_mkdir$:
            call    fat_handle_mkdir$
            jr      fw_after$

fw_rmdir$:
            call    fat_handle_rmdir$

fw_after$:
            ld      de,(fat_current_req$)
fw_free$:
            ld      hl,#__usr_heap
            call    _mem_free
            jp      fw_loop$

            ;; ----------------------------------------------------------------
            ;; <de> <= _fat_init()
            ;; ----------------------------------------------------------------
            ;; idempotent shared initialization. the first caller creates:
            ;;   - queue event
            ;;   - i/o completion event
            ;;   - worker thread
            ;;
            ;; if another thread arrives mid-initialization it sees FAT_EBUSY.
            ;; that keeps the code compact and still makes the race explicit.
            ;; ----------------------------------------------------------------
_fat_init::
            call    _ir_disable
            ld      a,(fat_init_state$)
            cp      #FAT_INIT_READY
            jr      z,fi_ready$
            cp      #FAT_INIT_NONE
            jr      nz,fi_busy$
            ld      a,#FAT_INIT_BUSY
            ld      (fat_init_state$),a
            call    _ir_enable

            ;; the request queue lives in _SYSVARS (which overlaps the tail and
            ;; is NOT zero-initialized), so start it empty before the worker runs
            ;; -- otherwise the first pop/push sees a garbage head/tail.
            ld      hl,#0x0000
            ld      (fat_req_head$),hl
            ld      (fat_req_tail$),hl

            ld      hl,#0x0000
            call    _evt_create
            ld      (fat_queue_event$),de
            ld      a,d
            or      e
            jr      z,fi_nomem$

            ld      hl,#0x0000
            call    _evt_create
            ld      (fat_io_event$),de
            ld      a,d
            or      e
            jr      z,fi_fail_queue_evt$

fi_after_guard_alloc$:
            ld      hl,#fat_worker$
            ld      de,#FAT_WORKER_STACK + 2048
            ld      bc,#0x0000
            push    bc                  ; thread_data = NONE
            ld      bc,#1
            push    bc                  ; bank = 1 (current/boot bank)
            call    _thread_create     ; callee-clean: pops both stacked args
            ld      (fat_worker_thread$),de
            ld      a,d
            or      e
            jr      z,fi_fail_both_evts$

            ex      de,hl               ; thread_create returned thread in de
            call    _thread_resume

            ld      hl,#_fat_purge_owner
            call    _owner_cleanup_register

            call    _ir_disable
            ld      a,#FAT_INIT_READY
            ld      (fat_init_state$),a
            call    _ir_enable
            ld      de,#FAT_OK
            ret

fi_ready$:
            call    _ir_enable
            ld      de,#FAT_OK
            ret

fi_busy$:
            call    _ir_enable
            ld      de,#FAT_EBUSY
            ret

fi_fail_both_evts$:
            ld      hl,(fat_io_event$)
            call    _evt_destroy
fi_fail_queue_evt$:
            ld      hl,(fat_queue_event$)
            call    _evt_destroy
fi_nomem$:
            call    _ir_disable
            xor     a
            ld      (fat_init_state$),a
            call    _ir_enable
            ld      de,#FAT_ENOMEM
            ret

            .area   _INITIALIZED

fat_req_head$:
            .dw     0x0000
fat_req_tail$:
            .dw     0x0000
fat_queue_event$:
            .dw     0x0000
fat_io_event$:
            .dw     0x0000
fat_worker_thread$:
            .dw     0x0000
fat_init_state$:
            .db     FAT_INIT_NONE
fat_wait_evt$:
            .dw     0x0000
fat_pop_req$:
            .dw     0x0000

            .area   _SYSVARS

fat_pos24$:
            .ds     3
fat_work_fs$:
            .ds     2
fat_work_dev$:
            .ds     2
fat_work_event$:
            .ds     2
fat_current_req$:
            .ds     2
            ;; single-sector FAT cache: which (dev, volume-relative sector) is
            ;; currently held in fat_sector$ as a FAT sector. fdev==0 means the
            ;; cache is invalid. Any fat_xfer_block$ (read or write of ANY block)
            ;; clears fdev, so a directory/data read or a FAT write never leaves a
            ;; stale hit; fat_get_fat_entry$ re-validates it after a real read.
            ;; Zeroed at OS entry (_SYSVARS clear) -> starts invalid.
fat_cache_fdev$:
            .ds     2
fat_cache_fsec$:
            .ds     2
            ;; destination buffer for the next fat_xfer_block$ read/write. defaults
            ;; to fat_sector$ (set by fat_read_block$/fat_write_block$); a file/dir
            ;; DATA read points it straight at the caller's user buffer via
            ;; fat_read_block_to$, so fat_sector$ (the FAT cache) is not disturbed.
fat_xfer_buf$:
            .ds     2
fat_submit_frame$:
            .ds     10
            .equ    fat_submit_op_flags$, (fat_submit_frame$ + 4)
            .equ    fat_submit_event$, (fat_submit_frame$ + 6)
            .equ    fat_submit_path_event$, (fat_submit_frame$ + 8)
fat_candidate_base$:
fat_file_ptr$:
fat_create_entry$:
            .ds     2
fat_cluster_shift$:
fat_file_secs_left$:
            .ds     1
fat_spc_raw$:
fat_file_sec_in_cluster$:
            .ds     1
fat_mbr_unsupported$:
fat_file_spc$:
            .ds     1
fat_tmp_num_fats$:
fat_file_shift$:
            .ds     1
fat_tmp_reserved$:
fat_file_buf$:
fat_create_cluster$:
            .ds     2
fat_tmp_root_entries$:
fat_file_pos_secs$:
            .ds     2
fat_tmp_spf$:
fat_file_size_secs$:
            .ds     2
fat_tmp_total$:
fat_file_end_secs$:
            .ds     2
fat_tmp_root_dir_sectors$:
fat_file_cluster$:
            .ds     2
fat_tmp_root_start$:
fat_file_tmp16$:
            .ds     2
fat_tmp_data_start$:
fat_file_is_write$:
            .ds     1
fat_file_at_boundary$:
            .ds     1
fat_tmp_clusters$:
            .ds     2
fat_lookup_path$:
            .ds     2
fat_lookup_more$:
            .ds     1
fat_path_saw_dot$:
            .ds     1
fat_char$:
            .ds     1
fat_fat_offset_tmp$:
            .ds     2
fat_tmp_cluster$:
            .ds     2
fat_offset_low$:
            .ds     1
fat_pair$:
            .ds     2
fat_lookup_dirent$:
            .ds     2
fat_entry_ptr$:
            .ds     2
fat_lookup_sector$:
            .ds     2
fat_lookup_entry_off$:
            .ds     1
fat_scan_remaining$:
            .ds     2
fat_lookup_cluster$:
            .ds     2
fat_name$:
            .ds     FAT_SHORT_NAME_LEN
fat_sector$:
            .ds     FAT_SECTOR_SIZE
fat_purge_owner$:
            .ds     2
fat_purge_prev$:
            .ds     2
fat_purge_req$:
            .ds     2
fat_purge_next$:
            .ds     2

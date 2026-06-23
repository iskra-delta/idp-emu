            ;; fat_mount.s
            ;;
            ;; FAT12/FAT16 mount parser and async mount submit paths.
            ;;
            ;; this file owns everything needed to turn a block device into one
            ;; mounted fat_fs_t:
            ;;   - boot-sector plausibility checks
            ;;   - full FAT12/FAT16 BPB parsing
            ;;   - MBR partition probing for hard disks
            ;;   - public async mount submission entry points
            ;;   - worker-side execution of queued mount requests
            ;;
            ;; it intentionally stops at metadata discovery. no directory walk
            ;; or file data access happens here; that lives in fat_lookup.s and
            ;; later file I/O modules. the split keeps mount-time code compact
            ;; and lets us optimize parsing without mixing it with path logic.
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module fat_mount

            .include "fat.inc"

            .globl  _fat_init
            .globl  _fat_mount_dev
            .globl  _fat_mount
            .globl  __find_dev_drv

            .globl  fat_ret_clean2$
            .globl  fat_alloc_req$
            .globl  fat_evt_reset$
            .globl  fat_complete_fs$
            .globl  fat_store_de$
            .globl  fat_load_de$
            .globl  fat_decode_req_evfdev$
            .globl  fat_queue_req$
            .globl  fat_req_base$
            .globl  fat_spc_shift$
            .globl  fat_prepare_fs_busy$
            .globl  fat_dev_open$
            .globl  fat_read_block$
            .globl  fat_work_fs$
            .globl  fat_work_dev$
            .globl  fat_work_event$
            .globl  fat_submit_event$
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
            .globl  fat_sector$

            .globl  fat_handle_mount$

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; fat_spc_shift$(<a> sectors_per_cluster) -> <a> shift
            ;; ----------------------------------------------------------------
            ;; FAT requires sectors/cluster to be a power of two. later file
            ;; code can use the same shift to replace general division/multiply
            ;; with simple shifts. carry set = invalid value.
            ;; ----------------------------------------------------------------
fat_spc_shift$:
            or      a
            jr      z,fss_invalid$
            ld      b,#0
fss_loop$:
            cp      #1
            jr      z,fss_done$
            srl     a
            jr      c,fss_invalid$      ; odd value > 1 => not a power of two
            inc     b
            jr      fss_loop$
fss_done$:
            ld      a,b
            or      a                   ; clear carry
            ret
fss_invalid$:
            scf
            ret

            ;; ----------------------------------------------------------------
            ;; fat_boot_plausible$() -> <a> 0/1
            ;; ----------------------------------------------------------------
            ;; fast low-cost filter used before we spend more code/data on full
            ;; parsing. it rejects obviously non-FAT blocks:
            ;;   - wrong 0x55aa signature
            ;;   - bytes/sector != 256
            ;;   - sectors/cluster not a power of two
            ;;   - mandatory FAT12/16 BPB fields missing
            ;;   - total sector count not representable in 16 bits today
            ;; ----------------------------------------------------------------
fat_boot_plausible$:
            ld      a,(fat_sector$ + FATBOOT_SIG_LO)
            cp      #0x55
            jr      nz,fbp_false$
            ld      a,(fat_sector$ + FATBOOT_SIG_HI)
            cp      #0xaa
            jr      nz,fbp_false$

            ld      a,(fat_sector$ + FATBOOT_BPS)
            or      a
            jr      nz,fbp_false$
            ld      a,(fat_sector$ + FATBOOT_BPS + 1)
            cp      #1
            jr      nz,fbp_false$

            ld      a,(fat_sector$ + FATBOOT_SPC)
            call    fat_spc_shift$
            jr      c,fbp_false$

            ld      hl,#fat_sector$ + FATBOOT_RESERVED
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,fbp_false$

            ld      a,(fat_sector$ + FATBOOT_NUM_FATS)
            or      a
            jr      z,fbp_false$

            ld      hl,#fat_sector$ + FATBOOT_ROOT_ENTRIES
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,fbp_false$

            ld      hl,#fat_sector$ + FATBOOT_SECTORS_PER_FAT
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,fbp_false$

            ld      hl,#fat_sector$ + FATBOOT_TOTAL16
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      nz,fbp_true$

            ld      hl,#fat_sector$ + FATBOOT_TOTAL32 + 2
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      nz,fbp_false$

            ld      hl,#fat_sector$ + FATBOOT_TOTAL32
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,fbp_false$

fbp_true$:
            ld      a,#1
            ret
fbp_false$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; fat_fill_fs_from_boot$(<hl> fs, <de> lba_base256) -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; full metadata parse once fat_boot_plausible$ said "this looks
            ;; like a FAT boot sector". the function still validates carefully
            ;; and returns a real FAT_* status.
            ;;
            ;; stored values stay volume-relative except for lba_base; later I/O
            ;; code will add lba_base when talking to the block driver.
            ;; ----------------------------------------------------------------
fat_fill_fs_from_boot$:
            push    de                  ; keep lba_base safe across parsing

            ;; fat_boot_plausible$ already validated signature, 256-byte
            ;; sectors and a legal power-of-two sectors/cluster value.
            ld      a,(fat_sector$ + FATBOOT_SPC)
            ld      (fat_spc_raw$),a
            call    fat_spc_shift$
            ld      (fat_cluster_shift$),a

            ;; load the BPB fields we rely on. the cheap zero/high-half checks
            ;; were already done by fat_boot_plausible$ on the same sector.
            ld      hl,#fat_sector$ + FATBOOT_RESERVED
            call    fat_load_de$
            ld      (fat_tmp_reserved$),de

            ld      a,(fat_sector$ + FATBOOT_NUM_FATS)
            ld      (fat_tmp_num_fats$),a

            ld      hl,#fat_sector$ + FATBOOT_ROOT_ENTRIES
            call    fat_load_de$
            ld      (fat_tmp_root_entries$),de

            ld      hl,#fat_sector$ + FATBOOT_SECTORS_PER_FAT
            call    fat_load_de$
            ld      (fat_tmp_spf$),de

            ;; total sector count: prefer total16, otherwise use total32 low.
            ld      hl,#fat_sector$ + FATBOOT_TOTAL16
            call    fat_load_de$
            ld      a,d
            or      e
            jr      nz,ffb_have_total$
            ld      hl,#fat_sector$ + FATBOOT_TOTAL32
            call    fat_load_de$
ffb_have_total$:
            ld      (fat_tmp_total$),de

            ;; root_dir_sectors = ceil(root_entries / 8) = (root_entries + 7)>>3
            ld      de,(fat_tmp_root_entries$)
            ld      a,e
            add     a,#7
            ld      e,a
            jr      nc,ffb_rds_ready$
            inc     d
ffb_rds_ready$:
            srl     d
            rr      e
            srl     d
            rr      e
            srl     d
            rr      e
            ld      (fat_tmp_root_dir_sectors$),de
            push    hl
            ld      bc,#FATFS_ROOT_DIR_SECTORS
            add     hl,bc
            call    fat_store_de$
            pop     hl

            ;; fat_start = reserved_sectors
            ld      de,(fat_tmp_reserved$)
            push    hl
            ld      bc,#FATFS_FAT_START
            add     hl,bc
            call    fat_store_de$
            pop     hl

            ;; root_start = reserved + num_fats * sectors_per_fat
            ld      hl,(fat_tmp_reserved$)
            ld      de,(fat_tmp_spf$)
            ld      a,(fat_tmp_num_fats$)
ffb_root_loop$:
            or      a
            jr      z,ffb_root_done$
            add     hl,de
            dec     a
            jr      ffb_root_loop$
ffb_root_done$:
            ld      (fat_tmp_root_start$),hl

            ;; data_start = root_start + root_dir_sectors
            ld      de,(fat_tmp_root_dir_sectors$)
            add     hl,de
            ld      (fat_tmp_data_start$),hl

            ;; data_start must lie inside the volume.
            ex      de,hl               ; de = data_start
            ld      hl,(fat_tmp_total$)
            or      a
            sbc     hl,de
            jp      c,ffb_corrupt_pop$
            jp      z,ffb_corrupt_pop$

            ;; total_clusters = (total - data_start) >> cluster_shift
            ld      a,(fat_cluster_shift$)
ffb_cls_loop$:
            or      a
            jr      z,ffb_cls_done$
            srl     h
            rr      l
            dec     a
            jr      ffb_cls_loop$
ffb_cls_done$:
            ld      a,h
            or      l
            jr      z,ffb_corrupt_pop$
            ld      (fat_tmp_clusters$),hl

            ;; commit the validated metadata in one linear pass. this avoids
            ;; repeating "add fs+offset / store / restore hl" scaffolding for
            ;; every field in fat_fs_t.
            pop     de                  ; de = saved lba_base
            ld      hl,(fat_work_fs$)
            inc     hl
            inc     hl                  ; hl = fs->lba_base
            call    fat_store_de$
            ld      de,(fat_tmp_total$)
            call    fat_store_de$
            ld      de,(fat_tmp_reserved$)
            call    fat_store_de$
            ld      de,(fat_tmp_spf$)
            call    fat_store_de$
            ld      de,(fat_tmp_root_entries$)
            call    fat_store_de$
            ld      de,(fat_tmp_root_dir_sectors$)
            call    fat_store_de$
            ld      de,(fat_tmp_reserved$)
            call    fat_store_de$       ; fat_start = reserved
            ld      de,(fat_tmp_root_start$)
            call    fat_store_de$
            ld      de,(fat_tmp_data_start$)
            call    fat_store_de$
            ld      de,(fat_tmp_clusters$)
            call    fat_store_de$

            ;; alloc_hint starts at cluster 2 for the future allocator.
            ld      de,#FAT_CLUSTER_FIRST
            call    fat_store_de$
            ld      a,(fat_spc_raw$)
            ld      (hl),a
            inc     hl
            ld      a,(fat_tmp_num_fats$)
            ld      (hl),a
            inc     hl

            ;; FAT12 if clusters < 4085, otherwise FAT16.
            ld      de,(fat_tmp_clusters$)
            ld      a,d
            cp      #0x0f
            jr      c,ffb_commit_fat12$
            jr      nz,ffb_commit_fat16$
            ld      a,e
            cp      #0xf5
            jr      c,ffb_commit_fat12$
ffb_commit_fat16$:
            ld      a,#16
            jr      ffb_commit_fatbits$
ffb_commit_fat12$:
            ld      a,#12
ffb_commit_fatbits$:
            ld      (hl),a
            inc     hl
            ld      (hl),#1             ; mounted = 1

            ld      hl,#FAT_OK
            ret

ffb_notsup_pop$:
            pop     bc
            ld      hl,#FAT_ENOTSUP
            ret
ffb_corrupt_pop$:
            pop     bc
            ld      hl,#FAT_ECORRUPT
            ret

            ;; ----------------------------------------------------------------
            ;; fat_mount_from_mbr$() -> <hl> rc
            ;; ----------------------------------------------------------------
            ;; reads the second half of the PC-style MBR, validates the 0x55aa
            ;; signature and scans up to four partition entries. for each non-
            ;; empty entry we try the stored start value directly and, if that
            ;; fails, doubled. this lets us tolerate either 256-byte or 512-byte
            ;; partition-base conventions without hardcoding one forever.
            ;; ----------------------------------------------------------------
fat_mount_from_mbr$:
            xor     a
            ld      (fat_mbr_unsupported$),a

            ld      hl,(fat_work_dev$)
            ld      de,#1
            call    fat_read_block$
            ld      a,h
            or      l
            ret     nz

            ld      a,(fat_sector$ + FATMBR_SIG_LO)
            cp      #0x55
            jp      nz,fmm_corrupt$
            ld      a,(fat_sector$ + FATMBR_SIG_HI)
            cp      #0xaa
            jp      nz,fmm_corrupt$

            ld      hl,#fat_sector$ + FATMBR_PART0
            ld      b,#4
fmm_scan$:
            push    bc
            push    hl                  ; original entry pointer for this round

            ld      de,#FATMBR_TYPE
            add     hl,de
            ld      a,(hl)
            or      a
            jr      z,fmm_next$

            ld      de,#(FATMBR_SIZE - FATMBR_TYPE)
            add     hl,de
            ld      a,(hl)
            inc     hl
            or      (hl)
            inc     hl
            or      (hl)
            inc     hl
            or      (hl)
            jr      z,fmm_next$

            pop     hl                  ; restore entry pointer
            push    hl
            ld      de,#FATMBR_START
            add     hl,de
            call    fat_load_de$        ; de = low 16 bits of start
            ld      (fat_candidate_base$),de
            inc     hl
            inc     hl
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,fmm_try_direct$
            ld      a,#1
            ld      (fat_mbr_unsupported$),a
            jr      fmm_next$

fmm_try_direct$:
            ld      hl,(fat_work_dev$)
            ld      de,(fat_candidate_base$)
            call    fat_read_block$
            ld      a,h
            or      l
            jr      nz,fmm_stop$
            call    fat_boot_plausible$
            or      a
            jr      nz,fmm_parse$

            ld      de,(fat_candidate_base$)
            ld      a,d
            and     #0x80              ; doubling would overflow 16 bits
            jr      nz,fmm_next$
            sla     e
            rl      d
            ld      (fat_candidate_base$),de
            ld      hl,(fat_work_dev$)
            call    fat_read_block$
            ld      a,h
            or      l
            jr      nz,fmm_stop$
            call    fat_boot_plausible$
            or      a
            jr      z,fmm_next$

fmm_parse$:
            ld      hl,(fat_work_fs$)
            ld      de,(fat_candidate_base$)
            call    fat_fill_fs_from_boot$
            jr      fmm_stop$

fmm_next$:
            pop     hl
            pop     bc
            ld      de,#FATMBR_PART_SIZE
            add     hl,de
            djnz    fmm_scan$

            ld      a,(fat_mbr_unsupported$)
            or      a
            jr      nz,fmm_notsup$
            ld      hl,#FAT_ENOENT
            ret
fmm_notsup$:
            ld      hl,#FAT_ENOTSUP
            ret

fmm_stop$:
            pop     bc
            pop     bc
            ret

fmm_corrupt$:
            ld      hl,#FAT_ECORRUPT
            ret

            ;; ----------------------------------------------------------------
            ;; fat_handle_mount$(<de> req)
            ;; ----------------------------------------------------------------
            ;; worker-side execution of one queued mount request. all mutable
            ;; worker scratch lives in _SYSVARS so the code can stay simple and
            ;; stack usage stays tiny.
            ;; ----------------------------------------------------------------
fat_handle_mount$:
            call    fat_decode_req_evfdev$

            ld      hl,(fat_work_fs$)
            ld      a,h
            or      l
            jr      z,fhm_bad$
            ld      de,(fat_work_dev$)
            ld      a,d
            or      e
            jr      z,fhm_bad$

            ld      hl,(fat_work_fs$)
            ld      de,(fat_work_dev$)
            call    fat_prepare_fs_busy$

            ld      hl,(fat_work_dev$)
            call    fat_dev_open$
            ld      a,h
            or      l
            jr      nz,fhm_eio$

            ld      hl,(fat_work_dev$)
            ld      de,#0
            call    fat_read_block$
            ld      a,h
            or      l
            jr      nz,fhm_finish_hl$

            call    fat_boot_plausible$
            or      a
            jr      z,fhm_try_mbr$

            ld      hl,(fat_work_fs$)
            ld      de,#0
            call    fat_fill_fs_from_boot$
            jr      fhm_finish_hl$

fhm_try_mbr$:
            call    fat_mount_from_mbr$
            jr      fhm_finish_hl$

fhm_bad$:
            ld      hl,#FAT_EINVAL
            jr      fhm_finish_hl$
fhm_eio$:
            ld      hl,#FAT_EIO

fhm_finish_hl$:
            ex      de,hl               ; de = final status
            ld      hl,(fat_work_fs$)
            ld      a,h
            or      l
            ret     z                   ; invalid request: nothing safe to poke
            ld      bc,(fat_work_event$)
            jp      fat_complete_fs$
            ;; ----------------------------------------------------------------
            ;; fat_mount_common$(<hl> fs, <de> dev, <bc> event) -> <de> rc
            ;; ----------------------------------------------------------------
            ;; shared submit path for mount-by-name and mount-by-device.
            ;; returns FAT_OK once the request is queued; completion is later
            ;; reported through event + fs->status.
            ;; ----------------------------------------------------------------
fat_mount_common$:
            ld      a,h
            or      l
            jr      z,fmc_invalid$
            ld      a,d
            or      e
            jr      z,fmc_invalid$

            push    bc
            push    de
            push    hl
            ld      h,b
            ld      l,c
            call    fat_evt_reset$
            pop     hl
            pop     de
            pop     bc

            ;; _fat_init's thread/event bring-up runs deep enough to clobber a
            ;; stacked event word; keep the caller's pointer off-stack now.
            ld      (fat_submit_event$),bc

            push    bc
            push    de
            push    hl
            call    _fat_init
            ld      a,d                 ; test _fat_init's result NOW: the pops
            or      e                   ; below restore de to the dev pointer,
            pop     hl                  ; and pop does not affect flags, so the
            pop     de                  ; z result survives to the jr below.
            pop     bc
            jr      z,fmc_inited$
            jp      fat_complete_fs$

fmc_inited$:
            push    de
            call    fat_prepare_fs_busy$
            pop     de

            push    hl                  ; save fs
            push    de                  ; save dev
            call    fat_alloc_req$

            jr      nz,fmc_have_req$
            pop     de                  ; drop dev
            pop     hl                  ; drop fs
            ld      de,#FAT_ENOMEM
            jp      fat_complete_fs$

fmc_have_req$:
            ;; fill the request. the heap block itself is system-owned; the
            ;; embedded req->owner field still records the logical caller
            ;; owner so dead-owner cleanup can unlink queued work later on.
            ld      bc,(fat_submit_event$)
            pop     de                  ; dev
            pop     hl                  ; fs
            push    de                  ; save dev
            push    hl                  ; save fs
            call    fat_req_base$
            ld      (hl),#FATREQ_OP_MOUNT
            inc     hl
            ld      (hl),a              ; flags = 0
            inc     hl
            ld      (hl),c              ; event lo
            inc     hl
            ld      (hl),b              ; event hi
            inc     hl
            pop     de                  ; fs
            call    fat_store_de$
            pop     de                  ; dev
            call    fat_store_de$
            xor     a
            ld      (hl),a              ; buf lo
            inc     hl
            ld      (hl),a              ; buf hi
            inc     hl
            ld      (hl),a              ; bytes lo
            inc     hl
            ld      (hl),a              ; bytes hi
            inc     hl
            ld      (hl),a              ; arg lo
            inc     hl
            ld      (hl),a              ; arg hi

            call    fat_queue_req$
            ret

fmc_invalid$:
            ld      de,#FAT_EINVAL
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _fat_mount_dev(<hl> fs, <de> dev, <stack> event)
            ;; ----------------------------------------------------------------
_fat_mount_dev::
            push    hl                  ; keep fs while we look at the stack
            ld      hl,#4
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked event pointer
            pop     hl                  ; restore fs
            call    fat_mount_common$
            jp      fat_ret_clean2$

            ;; ----------------------------------------------------------------
            ;; <de> <= _fat_mount(<hl> fs, <de> dev_name, <stack> event)
            ;; ----------------------------------------------------------------
            ;; resolves the device by name first. if no such device exists we
            ;; complete the request immediately with FAT_ENODEV.
            ;; ----------------------------------------------------------------
_fat_mount::
            push    hl                  ; keep fs while we look at the stack
            ld      hl,#4
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = stacked event pointer
            pop     hl                  ; restore fs
            ld      a,h
            or      l
            jr      z,fm_invalid$
            ex      de,hl               ; hl = device name, de = fs
            ld      a,h
            or      l
            jr      z,fm_invalid_restore$
            push    bc                  ; keep event across the lookup
            push    de                  ; keep fs across the lookup
            call    __find_dev_drv
            ex      de,hl               ; de = dev, hl = driver (ignored now)
            pop     hl                  ; hl = fs
            pop     bc                  ; bc = event
            ld      a,d
            or      e
            jr      nz,fm_have_dev$
            push    bc
            push    hl
            ld      h,b
            ld      l,c
            call    fat_evt_reset$
            pop     hl                  ; fs
            pop     bc                  ; event
            ld      de,#FAT_ENODEV
            call    fat_complete_fs$
            jp      fat_ret_clean2$

fm_have_dev$:
            call    fat_mount_common$
            jp      fat_ret_clean2$

fm_invalid_restore$:
            ex      de,hl               ; restore hl = fs for invalid return
fm_invalid$:
            ld      de,#FAT_EINVAL
            jp      fat_ret_clean2$

            ;; process.s
            ;;
            ;; minimal process subsystem for PartOS OS layer
            ;;
            ;; this module ports the old YOS process core into the new assembly
            ;; OS layer on top of kernel primitives:
            ;;
            ;;   - process objects are sysobjs linked through one global list
            ;;   - process_start() creates the process record, spawns the main
            ;;     thread and resumes it
            ;;   - process_load_image() accepts an XL image already present in
            ;;     memory, validates its header, relocates it in place, computes
            ;;     the real entry address and starts the process
            ;;   - process_reap() frees events, timers, services and owned heap
            ;;     blocks once the last thread referencing the process is gone
            ;;
            ;; PartOS does not yet provide a filesystem-bound application loader
            ;; ABI, so the image loader here is intentionally memory-based: an
            ;; OS caller loads bytes into a heap block first, then hands that
            ;; block to process_load_image().
            ;;
            ;; 2026-06-17   tstih
            .module process

            .include "process.inc"

            .globl  _process_start
            .globl  _process_load_image
            .globl  _process_reap
            .globl  _process_exit
            .globl  _process_relocate
            .globl  ___process_relocate

            .globl  _process_first
            .globl  _process_last_error

            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_exit
            .globl  _thread_current
            .globl  _thread_first_suspended
            .globl  _thread_first_running
            .globl  _thread_first_waiting
            .globl  _thread_first_terminated

            .globl  _so_create
            .globl  _so_destroy
            .globl  _evt_destroy
            .globl  _tmr_uninstall
            .globl  _svc_unregister
            .globl  _mem_free_owner
            .globl  __evt_first
            .globl  __tmr_first
            .globl  __svc_first
            .globl  __usr_heap
            .globl  __sys_heap
            .globl  _ir_disable
            .globl  _ir_enable

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; process_start$(<hl> name, <de> entry, <bc> stack_size)
            ;; ----------------------------------------------------------------
            ;; internal helper used by both the public process_start() wrapper
            ;; and the XL image loader. caller must already hold the interrupt
            ;; bracket so scratch variables stay private.
            ;;
            ;; returns:
            ;;   de = process_t* or 0
            ;; ----------------------------------------------------------------
process_start$:
            ld      (ps_name$),hl
            ld      (ps_entry$),de
            ld      (ps_stack$),bc

            ;; p = so_create(&process_first, PROCESS_SIZE, NONE)
            ld      de,#0x0000
            push    de
            ld      de,#PROCESS_SIZE
            ld      hl,#_process_first
            call    _so_create
            pop     bc
            ld      a,d
            or      e
            ret     z
            ld      (ps_p$),de

            ;; p->pflags = 0
            ex      de,hl
            ld      bc,#PROCESS_PFLAGS
            add     hl,bc
            xor     a
            ld      (hl),a

            ;; zero p->pname[] up front so NULL / short names are cheap and the
            ;; struct stays deterministic for debugging.
            inc     hl                  ; hl = p->pname
            push    hl
            ld      b,#MAX_PNAME_LEN
psz_name$:
            ld      (hl),a
            inc     hl
            djnz    psz_name$
            pop     hl                  ; hl = p->pname

            ;; bounded name copy (max 7 chars + trailing NUL)
            ld      de,(ps_name$)
            ld      a,d
            or      e
            jr      z,psz_name_done$
            ld      b,#(MAX_PNAME_LEN - 1)
psz_copy$:
            ld      a,(de)
            ld      (hl),a
            or      a
            jr      z,psz_name_done$
            inc     de
            inc     hl
            djnz    psz_copy$
            xor     a
            ld      (hl),a
psz_name_done$:

            ;; p->main_thread = thread_create(entry, stack_size, p)
            ld      hl,(ps_entry$)
            ld      de,(ps_stack$)
            ld      bc,(ps_p$)
            push    bc
            call    _thread_create
            ld      a,d
            or      e
            jr      nz,psz_thread_ok$

            ;; thread creation failed: destroy the half-built process object
            ld      de,(ps_p$)
            ld      hl,#_process_first
            call    _so_destroy
            ld      de,#0x0000
            ret

psz_thread_ok$:
            ld      hl,(ps_p$)
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            ld      (hl),e
            inc     hl
            ld      (hl),d

            ;; the process pointer was already passed into thread_create(), but
            ;; rewrite it here explicitly so process semantics stay obvious.
            push    de
            pop     hl                  ; hl = thread
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      de,(ps_p$)
            ld      (hl),e
            inc     hl
            ld      (hl),d

            ;; move the new main thread suspended -> running
            ld      hl,(ps_p$)
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            push    de
            pop     hl
            call    _thread_resume

            ld      de,(ps_p$)
            ret

            ;; ----------------------------------------------------------------
            ;; process_find_owned_sysobj$(<hl> first, <de> owner) -> <hl> match
            ;; ----------------------------------------------------------------
            ;; scans a sysobj-based list whose owner pointer sits at offset +2.
            ;; used for events, timers and services during process reap.
            ;; ----------------------------------------------------------------
process_find_owned_sysobj$:
pfos_loop$:
            ld      a,h
            or      l
            ret     z
            push    hl
            inc     hl
            inc     hl                  ; hl = item->owner
            ld      a,(hl)
            cp      e
            jr      nz,pfos_next$
            inc     hl
            ld      a,(hl)
            cp      d
            jr      nz,pfos_next$
            pop     hl
            ret
pfos_next$:
            pop     hl
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      pfos_loop$

            ;; ----------------------------------------------------------------
            ;; <a> <= process_find_thread_in_list$(<hl> first, <de> process)
            ;; ----------------------------------------------------------------
            ;; returns 1 if any thread on the supplied list belongs to `process`.
            ;; ----------------------------------------------------------------
process_find_thread_in_list$:
pftl_loop$:
            ld      a,h
            or      l
            jr      z,pftl_no$
            push    hl
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      a,(hl)
            cp      e
            jr      nz,pftl_next$
            inc     hl
            ld      a,(hl)
            cp      d
            jr      nz,pftl_next$
            pop     hl
            ld      a,#1
            ret
pftl_next$:
            pop     hl
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      pftl_loop$
pftl_no$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= process_has_threads$(<de> process)
            ;; ----------------------------------------------------------------
            ;; checks every scheduler state list for any remaining thread whose
            ;; parent process pointer still matches `process`.
            ;; ----------------------------------------------------------------
process_has_threads$:
            ld      hl,(_thread_first_running)
            call    process_find_thread_in_list$
            or      a
            ret     nz
            ld      hl,(_thread_first_suspended)
            call    process_find_thread_in_list$
            or      a
            ret     nz
            ld      hl,(_thread_first_waiting)
            call    process_find_thread_in_list$
            or      a
            ret     nz
            ld      hl,(_thread_first_terminated)
            jr      process_find_thread_in_list$

            ;; ----------------------------------------------------------------
            ;; process_prepare_image$(<hl> img, <de> img_size) -> <de> entry
            ;; ----------------------------------------------------------------
            ;; validates a heap-backed XL image, relocates it in place and
            ;; returns the true relocated entry address.
            ;;
            ;; returns:
            ;;   a  = 0 on success, 1 on failure
            ;;   de = entry address on success
            ;; ----------------------------------------------------------------
process_prepare_image$:
            push    ix
            push    hl
            pop     ix                  ; ix = img

            ;; must at least cover the 12-byte XL header
            ld      a,d
            or      a
            jr      nz,ppi_size_ok$
            ld      a,e
            cp      #XL_HDR_SIZE
            jr      nc,ppi_size_ok$
            jr      ppi_fail$
ppi_size_ok$:
            ;; validate magic and current version
            ld      a,XL_OFF_MAGIC0(ix)
            cp      #'X'
            jr      nz,ppi_fail$
            ld      a,XL_OFF_MAGIC1(ix)
            cp      #'L'
            jr      nz,ppi_fail$
            ld      a,XL_OFF_VERSION(ix)
            cp      #0x01
            jr      nz,ppi_fail$

            ;; total image bytes must cover:
            ;;   XL header + relocation table + linked code/data payload
            ld      c,XL_OFF_RELOC_CNT(ix)
            ld      b,XL_OFF_RELOC_CNT+1(ix)
            ld      l,c
            ld      h,b
            add     hl,hl
            add     hl,hl              ; hl = reloc_count * 4
            ld      bc,#XL_HDR_SIZE
            add     hl,bc              ; hl = header + reloc table
            ld      c,XL_OFF_CODE_SIZE(ix)
            ld      b,XL_OFF_CODE_SIZE+1(ix)
            add     hl,bc              ; hl = total required bytes
            or      a
            sbc     hl,de              ; required - img_size
            jr      c,ppi_span_ok$
            ld      a,h
            or      l
            jr      z,ppi_span_ok$
            jr      ppi_fail$
ppi_span_ok$:
            ;; entry must land inside the emitted payload span
            ld      e,XL_OFF_CODE_SIZE(ix)
            ld      d,XL_OFF_CODE_SIZE+1(ix)
            ld      a,d
            or      e
            jr      z,ppi_fail$
            ld      l,XL_OFF_ENTRY(ix)
            ld      h,XL_OFF_ENTRY+1(ix)
            or      a
            sbc     hl,de
            jr      nc,ppi_fail$

            ;; relocate the payload in place before computing the final entry
            push    ix
            pop     hl
            call    process_relocate_impl$
            or      a
            jr      nz,ppi_fail$

            ;; de = code base = img + XL_HDR_SIZE + reloc_count * 4
            ld      c,XL_OFF_RELOC_CNT(ix)
            ld      b,XL_OFF_RELOC_CNT+1(ix)
            ld      l,c
            ld      h,b
            add     hl,hl
            add     hl,hl
            ld      bc,#XL_HDR_SIZE
            add     hl,bc
            push    ix
            pop     de
            add     hl,de

            ;; hl = entry = code base + linked entry offset
            ld      e,XL_OFF_ENTRY(ix)
            ld      d,XL_OFF_ENTRY+1(ix)
            add     hl,de
            ex      de,hl
            xor     a
            pop     ix
            ret

ppi_fail$:
            ld      a,#1
            pop     ix
            ret

            ;; ----------------------------------------------------------------
            ;; process_relocate_impl$(<hl> img)
            ;; ----------------------------------------------------------------
            ;; walk the XL relocation table and patch the payload in place.
            ;; word relocations add the full load base, while byte relocations
            ;; add only the low or high base byte according to flag bit 0.
            ;;
            ;; returns:
            ;;   a = 0 on success, 1 on malformed relocation entry
            ;; ----------------------------------------------------------------
process_relocate_impl$:
            push    ix
            push    hl
            pop     ix                  ; ix = img

            ld      c,XL_OFF_RELOC_CNT(ix)
            ld      b,XL_OFF_RELOC_CNT+1(ix)

            ld      de,#XL_HDR_SIZE
            add     hl,de
            ex      de,hl               ; de = reloc_ptr

            ld      l,c
            ld      h,b
            add     hl,hl
            add     hl,hl
            add     hl,de
            push    hl
            pop     ix                  ; ix = code_ptr

prl_loop$:
            ld      a,b
            or      c
            jr      z,prl_ok$

            ld      a,(de)
            ld      l,a
            inc     de
            ld      a,(de)
            ld      h,a                 ; hl = patch offset
            inc     de
            ld      a,(de)              ; a = relocation size (1 or 2)
            inc     de                  ; de = flags byte

            push    de
            push    bc
            push    ix
            pop     bc
            add     hl,bc               ; hl = patch location

            cp      #2
            jr      z,prl_word$
            cp      #1
            jr      z,prl_byte$
            pop     bc
            pop     de
            ld      a,#1
            pop     ix
            ret

prl_word$:
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            push    hl
            push    ix
            pop     bc
            ex      de,hl
            add     hl,bc
            pop     de                  ; de = address of high byte
            ld      a,l
            dec     de
            ld      (de),a
            inc     de
            ld      a,h
            ld      (de),a
            jr      prl_next$

prl_byte$:
            ld      a,(de)              ; flags
            and     #0x01
            jr      nz,prl_byte_msb$

            ld      a,(hl)
            push    ix
            pop     bc
            add     a,c
            ld      (hl),a
            jr      prl_next$

prl_byte_msb$:
            ld      a,(hl)
            push    ix
            pop     bc
            add     a,b
            ld      (hl),a

prl_next$:
            pop     bc
            pop     de
            inc     de                  ; skip flags byte
            dec     bc
            jr      prl_loop$

prl_ok$:
            xor     a
            pop     ix
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _process_start(<hl> name, <de> entry, <stack> stack_size)
            ;; ----------------------------------------------------------------
            ;; public C-callable wrapper around process_start$.
            ;; ----------------------------------------------------------------
_process_start::
            call    _ir_disable
            ld      (ps_name$),hl
            ld      (ps_entry$),de
            ld      hl,#2
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      hl,(ps_name$)
            ld      de,(ps_entry$)
            call    process_start$
            call    _ir_enable
            pop     hl
            pop     bc                  ; drop stacked stack_size
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; <a> <= _process_relocate(<hl> img)
            ;; ----------------------------------------------------------------
            ;; public C-callable XL relocator wrapper.
            ;; ----------------------------------------------------------------
_process_relocate::
___process_relocate::
            jp      process_relocate_impl$

            ;; ----------------------------------------------------------------
            ;; <de> <= _process_load_image(<hl> name, <de> img,
            ;;                              <stack> img_size, stack_size)
            ;; ----------------------------------------------------------------
            ;; relocates an already-loaded XL image in place, computes its entry
            ;; point, starts it and transfers the backing heap block's owner to
            ;; the new process so the image is reclaimed automatically later.
            ;;
            ;; contract:
            ;;   - img must point at a heap-allocated XL image buffer
            ;;   - img_size must cover the entire image
            ;; ----------------------------------------------------------------
_process_load_image::
            call    _ir_disable
            ld      (pli_name$),hl
            ld      (pli_img$),de

            ld      hl,#2
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      (pli_size$),bc
            inc     hl
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      (pli_stack$),bc

            xor     a
            ld      (_process_last_error),a

            ld      hl,(pli_img$)
            ld      de,(pli_size$)
            call    process_prepare_image$
            jr      z,pli_prepared$
            ld      a,#PROCESS_LOAD_ERR_XL_INVALID
            ld      (_process_last_error),a
            ld      de,#0x0000
            jr      pli_done$

pli_prepared$:
            ld      (pli_entry$),de
            ld      hl,(pli_name$)
            ld      de,(pli_entry$)
            ld      bc,(pli_stack$)
            call    process_start$
            ld      a,d
            or      e
            jr      nz,pli_started$
            ld      a,#PROCESS_LOAD_ERR_XL_START
            ld      (_process_last_error),a
            jr      pli_done$

pli_started$:
            ;; Transfer the owning heap block from the temporary loader owner
            ;; to the final process object. owner lives 5 bytes before payload:
            ;;   img - BLK_SIZE + SYSOBJ_OWNER = img - 5
            ld      hl,(pli_img$)
            ld      bc,#(BLK_SIZE - SYSOBJ_OWNER)
            or      a
            sbc     hl,bc
            ld      (hl),e
            inc     hl
            ld      (hl),d

pli_done$:
            call    _ir_enable
            pop     hl
            pop     bc                  ; drop stacked img_size
            pop     bc                  ; drop stacked stack_size
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; _process_reap(<hl> process)
            ;; ----------------------------------------------------------------
            ;; destroys the process record once no threads still reference it.
            ;; owned events, timers, services and heap blocks are reclaimed in
            ;; that order so global lists never retain stale pointers.
            ;; ----------------------------------------------------------------
_process_reap::
            call    _ir_disable
            ld      a,h
            or      l
            jr      z,pr_done$
            ld      (pr_p$),hl
            ex      de,hl
            call    process_has_threads$
            or      a
            jr      nz,pr_done$

            ;; process is now idle; clear the cached main-thread pointer
            ld      hl,(pr_p$)
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a

pr_evt_loop$:
            ld      de,(pr_p$)
            ld      hl,(__evt_first)
            call    process_find_owned_sysobj$
            ld      a,h
            or      l
            jr      z,pr_tmr_loop$
            call    _evt_destroy
            jr      pr_evt_loop$

pr_tmr_loop$:
            ld      de,(pr_p$)
            ld      hl,(__tmr_first)
            call    process_find_owned_sysobj$
            ld      a,h
            or      l
            jr      z,pr_svc_loop$
            call    _tmr_uninstall
            jr      pr_tmr_loop$

pr_svc_loop$:
            ld      de,(pr_p$)
            ld      hl,(__svc_first)
            call    process_find_owned_sysobj$
            ld      a,h
            or      l
            jr      z,pr_free_heap$
            call    _svc_unregister
            jr      pr_svc_loop$

pr_free_heap$:
            ld      de,(pr_p$)
            push    de
            ld      hl,#__usr_heap
            call    _mem_free_owner
            pop     de
            ld      hl,#__sys_heap
            call    _mem_free_owner

            ld      de,(pr_p$)
            ld      hl,#_process_first
            call    _so_destroy

pr_done$:
            jp      _ir_enable

            ;; ----------------------------------------------------------------
            ;; _process_exit()
            ;; ----------------------------------------------------------------
            ;; terminate the current thread; thread cleanup will later notice
            ;; whether that was the last thread owned by the process and reap
            ;; the process record if appropriate.
            ;; ----------------------------------------------------------------
_process_exit::
            ld      hl,(_thread_current)
            ld      a,h
            or      l
            ret     z
            jp      _thread_exit

            .area   _INITIALIZED

_process_first::
            .dw     0x0000

_process_last_error::
            .db     PROCESS_LOAD_OK

            .area   _SYSVARS

pli_name$:
ps_name$:
            .ds     2
pli_img$:
            .ds     2
pli_size$:
ps_entry$:
pr_p$:
            .ds     2
pli_stack$:
ps_stack$:
            .ds     2
pli_entry$:
ps_p$:
            .ds     2

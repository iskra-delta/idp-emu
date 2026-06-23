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
            ;;   - __process_reap() frees events, timers, services and owned
            ;;     heap blocks once the last thread referencing the process is
            ;;     gone
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
            .globl  _process_load_com
            .globl  _process_wait
            .globl  __process_reap
            .globl  _process_exit
            .globl  __process_relocate

            .globl  _process_first
            .globl  _process_last_error
            .globl  _process_last_stage
            .globl  _process_last_result

            .globl  _thread_create
            .globl  _thread_resume
            .globl  _thread_exit
            .globl  _thread_current
            .globl  _thread_first_suspended
            .globl  _thread_first_running
            .globl  _thread_first_waiting
            .globl  _thread_first_terminated
            .globl  __thread_cleanup_terminated

            .globl  __so_create
            .globl  __so_destroy
            .globl  _set_cleanup
            .globl  _evt_destroy
            .globl  _tmr_uninstall
            .globl  _svc_unregister
            .globl  __mem_free_owner
            .globl  __evt_first
            .globl  __tmr_first
            .globl  __svc_first
            .globl  __usr_heap
            .globl  __sys_heap
            .globl  _ir_disable
            .globl  _ir_enable

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; process_resume_child$(<hl> t) -> <de> t | 0
            ;; ----------------------------------------------------------------
            ;; resume one freshly created child thread and, when there is a
            ;; current caller thread, rotate the running list so the child sits
            ;; immediately after that caller. This keeps cooperative launches
            ;; responsive: one rst 0x18 from the parent reaches the child
            ;; directly instead of falling into the idle thread first.
            ;;
            ;; caller must already hold the interrupt bracket.
            ;; ----------------------------------------------------------------
process_resume_child$:
            ld      a,h
            or      l
            ret     z
            ld      de,(_thread_current)
            ld      a,d
            or      e
            jp      z,_thread_resume

            push    de                  ; save current
            call    _thread_resume      ; de = resumed child
            pop     hl                  ; hl = current
            ld      a,d
            or      e
            ret     z

            push    de
            pop     ix                  ; ix = child

            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            dec     hl                  ; bc = current->next

            ld      e,0(ix)
            ld      d,1(ix)             ; de = child->next (old running head)
            push    hl
            ld      hl,#_thread_first_running
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; running head = old head again
            pop     hl

            ld      0(ix),c
            ld      1(ix),b             ; child->next = old current->next
            push    ix
            pop     de                  ; de = child
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; current->next = child
            ret

            ;; ----------------------------------------------------------------
            ;; process_thread_cleanup$(<hl> thread)
            ;; ----------------------------------------------------------------
            ;; per-thread destructor hook for the current one-thread-per-process
            ;; launch path. when the main thread object is finally freed, look up
            ;; its owning process and ask the process layer to reap it if no
            ;; other threads still belong to that process.
            ;; ----------------------------------------------------------------
process_thread_cleanup$:
            ld      bc,#THREAD_PROCESS
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl
            ld      a,h
            or      l
            ret     z
            jp      __process_reap

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

            ;; p = __so_create(&process_first, PROCESS_SIZE, NONE)
            ld      de,#0x0000
            push    de
            ld      de,#PROCESS_SIZE
            ld      hl,#_process_first
            call    __so_create
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

            ;; zero p->pname[] and p->cmdline up front so NULL / short names are
            ;; cheap and the struct stays deterministic for debugging.
            inc     hl                  ; hl = p->pname
            push    hl
            ld      b,#MAX_PNAME_LEN
psz_name$:
            ld      (hl),a
            inc     hl
            djnz    psz_name$
            ld      (hl),a              ; p->cmdline lo = 0
            inc     hl
            ld      (hl),a              ; p->cmdline hi = 0
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

            ;; p->main_thread = thread_create(entry, stack_size, bank, p).
            ;; thread_data = the process itself (the kernel keeps it opaque);
            ;; bank = the process arena bank (bank 1 for now; cross-bank process
            ;; placement is a later step).
            ld      hl,(ps_entry$)
            ld      de,(ps_stack$)
            ld      bc,(ps_p$)
            push    bc                  ; thread_data = the process
            ld      bc,#1
            push    bc                  ; bank = 1
            call    _thread_create     ; callee-clean: pops both stacked args
            ld      a,d
            or      e
            jr      nz,psz_thread_ok$

            ;; thread creation failed: destroy the half-built process object
            ld      de,(ps_p$)
            ld      hl,#_process_first
            call    __so_destroy
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

            ;; the main-thread object carries the process reap hook. once the
            ;; thread sysobj itself is destroyed, the hook checks whether that
            ;; was the last thread and frees the process if so.
            ld      hl,(ps_p$)
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            push    de
            pop     hl
            ld      de,#process_thread_cleanup$
            call    _set_cleanup

            ;; move the new main thread suspended -> running. place it right
            ;; after the current thread so one cooperative yield from the
            ;; parent reaches the child immediately even if the periodic tick is
            ;; not firing yet.
            ld      hl,(ps_p$)
            ld      bc,#PROCESS_MAIN_THREAD
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            push    de
            pop     hl
            call    process_resume_child$

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
            ;; <a> <= process_is_live$(<de> process)
            ;; ----------------------------------------------------------------
            ;; returns 1 while the exact process pointer is still present on the
            ;; global process list, else 0.
            ;; ----------------------------------------------------------------
process_is_live$:
            ld      hl,(_process_first)
pil_loop$:
            ld      a,h
            or      l
            jr      z,pil_no$
            ld      a,l
            cp      e
            jr      nz,pil_next$
            ld      a,h
            cp      d
            jr      nz,pil_next$
            ld      a,#1
            ret
pil_next$:
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      pil_loop$
pil_no$:
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
            ;; process_parse_com$(<hl> img, <de> img_size)
            ;;                         -> <hl> inner_xl, <de> xl_size,
            ;;                            <bc> stack_size
            ;; ----------------------------------------------------------------
            ;; validates the outer COM header and returns the embedded XL span
            ;; plus the requested process stack size.
            ;;
            ;; returns:
            ;;   a  = 0 on success, 1 on failure
            ;;   hl = embedded XL image pointer
            ;;   de = embedded XL image size
            ;;   bc = requested stack size
            ;; ----------------------------------------------------------------
process_parse_com$:
            push    ix
            push    hl
            pop     ix                  ; ix = img

            ;; must at least cover the fixed 16-byte COM header
            ld      a,d
            or      a
            jr      nz,ppc_size_ok$
            ld      a,e
            cp      #COM_HDR_SIZE
            jr      nc,ppc_size_ok$
            jp      ppc_fail$
ppc_size_ok$:
            ld      a,COM_OFF_MAGIC0(ix)
            cp      #'C'
            jr      nz,ppc_fail$
            ld      a,COM_OFF_MAGIC1(ix)
            cp      #'M'
            jr      nz,ppc_fail$
            ld      a,COM_OFF_VERSION(ix)
            cp      #0x01
            jr      nz,ppc_fail$

            ld      c,COM_OFF_STACK_SIZE(ix)
            ld      b,COM_OFF_STACK_SIZE+1(ix)
            ld      a,b
            or      c
            jr      z,ppc_fail$

            ld      l,COM_OFF_XL_OFFSET(ix)
            ld      h,COM_OFF_XL_OFFSET+1(ix)
            ld      a,h
            or      a
            jr      nz,ppc_off_ok$
            ld      a,l
            cp      #COM_HDR_SIZE
            jr      c,ppc_fail$
ppc_off_ok$:
            push    bc                  ; save stack_size
            push    hl                  ; save xl_offset
            ld      c,COM_OFF_XL_SIZE(ix)
            ld      b,COM_OFF_XL_SIZE+1(ix)
            ld      a,b
            or      c
            jr      z,ppc_fail_saved$
            add     hl,bc               ; hl = xl_offset + xl_size
            or      a
            sbc     hl,de               ; required - img_size
            jr      c,ppc_span_ok$
            ld      a,h
            or      l
            jr      nz,ppc_fail_saved$
ppc_span_ok$:
            pop     hl                  ; hl = xl_offset
            pop     bc                  ; bc = stack_size
            ld      e,COM_OFF_XL_SIZE(ix)
            ld      d,COM_OFF_XL_SIZE+1(ix)
            push    bc                  ; preserve stack_size for the return ABI
            push    ix
            pop     bc                  ; bc = img base
            add     hl,bc               ; hl = embedded XL
            pop     bc                  ; restore stack_size

            ;; the outer COM header carries an entry hint for the embedded XL.
            ;; require that hint to match the inner XL header before the loader
            ;; accepts the image, so corrupt/misaligned wrappers fail early.
            ld      a,d
            or      a
            jr      nz,ppc_xl_hdr_ok$
            ld      a,e
            cp      #XL_HDR_SIZE
            jr      c,ppc_fail_post$
ppc_xl_hdr_ok$:
            push    bc
            push    hl
            ld      bc,#XL_OFF_ENTRY
            add     hl,bc
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            pop     hl
            pop     bc
            ld      a,e
            cp      COM_OFF_ENTRY_HINT(ix)
            jr      nz,ppc_fail_post$
            ld      a,d
            cp      COM_OFF_ENTRY_HINT+1(ix)
            jr      nz,ppc_fail_post$

            ld      e,COM_OFF_XL_SIZE(ix)
            ld      d,COM_OFF_XL_SIZE+1(ix)
            xor     a
            pop     ix
            ret

ppc_fail_post$:
            ld      a,#1
            pop     ix
            ret

ppc_fail_saved$:
            pop     hl
            pop     bc
ppc_fail$:
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
            ;; <a> <= __process_relocate(<hl> img)
            ;; ----------------------------------------------------------------
            ;; public C-callable XL relocator wrapper.
            ;; ----------------------------------------------------------------
__process_relocate::
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
            ;; <de> <= _process_load_com(<hl> name, <de> img, <stack> img_size)
            ;; ----------------------------------------------------------------
            ;; validates one COM header, resolves the embedded XL image,
            ;; relocates it in place, starts it, and transfers the outer heap
            ;; block's owner to the new process.
            ;; ----------------------------------------------------------------
_process_load_com::
            call    _ir_disable
            ld      (pli_name$),hl
            ld      (pli_img$),de

            ld      hl,#2
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      (pli_size$),bc

            xor     a
            ld      (_process_last_error),a
            ld      (_process_last_stage),a
            ld      hl,#_process_last_result
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     a
            ld      (_process_last_stage),a

            ld      hl,(pli_img$)
            ld      de,(pli_size$)
            call    process_parse_com$
            jr      z,plc_parsed$
            ld      a,#PROCESS_LOAD_ERR_COM_INVALID
            ld      (_process_last_error),a
            ld      de,#0x0000
            jr      plc_done$

plc_parsed$:
            ld      a,#2
            ld      (_process_last_stage),a
            ld      (plc_inner$),hl
            ld      (plc_inner_size$),de
            ld      (pli_stack$),bc

            ld      hl,(plc_inner$)
            ld      de,(plc_inner_size$)
            call    process_prepare_image$
            jr      z,plc_prepared$
            ld      a,#PROCESS_LOAD_ERR_XL_INVALID
            ld      (_process_last_error),a
            ld      de,#0x0000
            jr      plc_done$

plc_prepared$:
            ld      a,#3
            ld      (_process_last_stage),a
            ld      (pli_entry$),de
            ld      hl,(pli_name$)
            ld      de,(pli_entry$)
            ld      bc,(pli_stack$)
            call    process_start$
            ld      a,d
            or      e
            jr      nz,plc_started$
            ld      a,#PROCESS_LOAD_ERR_XL_START
            ld      (_process_last_error),a
            ld      de,#0x0000
            jr      plc_done$

plc_started$:
            ld      a,#4
            ld      (_process_last_stage),a
            ld      hl,#_process_last_result
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ;; transfer the entire COM buffer's heap owner to the new process
            ld      hl,(pli_img$)
            ld      bc,#(BLK_SIZE - SYSOBJ_OWNER)
            or      a
            sbc     hl,bc
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ld      a,#5
            ld      (_process_last_stage),a

plc_done$:
            call    _ir_enable
            pop     hl
            pop     bc                  ; drop stacked img_size
            jp      (hl)

            ;; ----------------------------------------------------------------
            ;; <de> <= _process_wait(<hl> process)
            ;; ----------------------------------------------------------------
            ;; foreground wait primitive used by the current shell. it keeps
            ;; yielding through the scheduler until the target process has been
            ;; reaped out of the global process list.
            ;; ----------------------------------------------------------------
_process_wait::
            ld      a,h
            or      l
            jr      z,pwait_done$
            ld      (pwait_target$),hl
pwait_loop$:
            call    _ir_disable
            ld      de,(pwait_target$)
            call    process_is_live$
            or      a
            jr      z,pwait_gone$
            call    _ir_enable
            rst     0x18
            call    _ir_disable
            call    __thread_cleanup_terminated
            call    _ir_enable
            jr      pwait_loop$
pwait_gone$:
            call    _ir_enable
pwait_done$:
            ld      de,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; __process_reap(<hl> process)
            ;; ----------------------------------------------------------------
            ;; destroys the process record once no threads still reference it.
            ;; owned events, timers, services and heap blocks are reclaimed in
            ;; that order so global lists never retain stale pointers.
            ;; ----------------------------------------------------------------
__process_reap::
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
            call    __mem_free_owner
            pop     de
            ld      hl,#__sys_heap
            call    __mem_free_owner

            ld      de,(pr_p$)
            ld      hl,#_process_first
            call    __so_destroy

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
_process_last_stage::
            .db     0x00
_process_last_result::
            .dw     0x0000

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
plc_inner$:
            .ds     2
plc_inner_size$:
            .ds     2
pwait_target$:
            .ds     2

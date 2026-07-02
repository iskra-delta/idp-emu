            ;; service.s
            ;;
            ;; a service is a named table of syscall function pointers. PartOS
            ;; syscalls are reached by querying a service by name and indexing
            ;; its table; the kernel exposes its own calls through the "partos"
            ;; service. hand-written replacement for yos service.c.
            ;;
            ;; lookups go through the shared list_find primitive with a small
            ;; string-compare match callback, so list traversal lives in one
            ;; place; register/unregister go through __so_create / __so_destroy
            ;; (which use list_insert / list_remove internally).
            ;;
            ;; layout (service_t):
            ;;   +0  next        (sysobj, list link)
            ;;   +2  owner       (sysobj)
            ;;   +4  name[16]    (nul-terminated service name)
            ;;   +20 fntable     (syscall function table)
            ;;
            ;; public c entry points use z80 sdcccall(1); 16-bit/pointer
            ;; results are returned in de:
            ;;
            ;;   _svc_register:    in  hl = name, de = fntable
            ;;                     out de = service or 0
            ;;   _svc_unregister:  in  hl = service
            ;;                     out de = freed service or 0
            ;;   _svc_query:       in  hl = name
            ;;                     out de = fntable or 0
            ;;   __svc_query_rst10: rst 0x10 bridge, hl = name -> de = fntable
            ;; ----------------------------------------------------------------
            ;; 2026-06-16   tstih
            .module service

            .globl  _svc_register
            .globl  _svc_register_ownerless
            .globl  _svc_unregister
            .globl  _svc_query
            .globl  _svc_init
            .globl  __svc_query_rst10
            .globl  __svc_first
            .globl  svc_reg_name$
            .globl  svc_reg_fntable$
            .globl  svc_reg_owner$
            .globl  svc_reg_service$
            .globl  __so_create
            .globl  __so_destroy
            .globl  __list_find$
            .globl  _list_find
            .globl  _owner_cleanup_register
            .globl  _thread_current
            .globl  _ir_disable
            .globl  _ir_enable

            .equ    SVC_NAME,           4
            .equ    MAX_SVC_NAME_LEN,   16
            .equ    SVC_FNTABLE,        SVC_NAME + MAX_SVC_NAME_LEN
            .equ    SVC_SIZE,           SVC_FNTABLE + 2

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; _svc_init()
            ;; ----------------------------------------------------------------
            ;; register the owner-death cleanup callback once so thread teardown
            ;; can unlink services owned by transient user threads.
            ;; ----------------------------------------------------------------
_svc_init::
            ld      a,(svc_owner_cleanup_ready$)
            or      a
            ret     nz
            ld      hl,#svc_purge_owner$
            call    _owner_cleanup_register
            ld      a,#1
            ld      (svc_owner_cleanup_ready$),a
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= svc_match_eq$(<hl> service, <de> name)
            ;; ----------------------------------------------------------------
            ;; list_find callback: returns 1 if the query name equals the
            ;; service's stored name, else 0. bounded by MAX_SVC_NAME_LEN.
            ;; ----------------------------------------------------------------
svc_match_eq$:
            push    bc
            ld      bc,#SVC_NAME
            add     hl,bc               ; hl -> service->name
            ld      b,#MAX_SVC_NAME_LEN
sme_loop$:
            ld      a,(de)
            cp      (hl)
            jr      nz,sme_false$
            or      a                   ; equal byte; nul -> end of both strings
            jr      z,sme_true$
            inc     hl
            inc     de
            djnz    sme_loop$
sme_true$:
            pop     bc
            ld      a,#1
            ret
sme_false$:
            pop     bc
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> <= svc_find_owned$(<hl> first, <de> owner)
            ;; ----------------------------------------------------------------
            ;; scan the service list for the first object whose owner equals
            ;; the requested owner pointer. returns hl = service or 0.
            ;; ----------------------------------------------------------------
svc_find_owned$:
            ld      a,h
            or      l
            ret     z
            push    hl
            ld      bc,#2
            add     hl,bc               ; hl = service->owner
            ld      a,(hl)
            cp      e
            jr      nz,sfo_next$
            inc     hl
            ld      a,(hl)
            cp      d
            jr      z,sfo_match$
sfo_next$:
            pop     hl
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = service->next
            ex      de,hl
            jr      svc_find_owned$
sfo_match$:
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; svc_purge_owner$(<de> owner)
            ;; ----------------------------------------------------------------
            ;; owner-death callback: remove every service owned by the dying
            ;; thread before its code/data blocks are reclaimed.
            ;; ----------------------------------------------------------------
svc_purge_owner$:
            ld      (svc_dead_owner$),de
spo_loop$:
            ld      de,(svc_dead_owner$)
            ld      hl,(__svc_first)
            call    svc_find_owned$
            ld      a,h
            or      l
            ret     z
            call    _svc_unregister
            jr      spo_loop$

            ;; ----------------------------------------------------------------
            ;; <de> <= svc_register_with_owner$(<hl> name, <de> fntable,
            ;;                                   <bc> owner)
            ;; ----------------------------------------------------------------
            ;; allocates a service sysobj, copies the name and stores the
            ;; function table, then links it into the service list.
            ;; ----------------------------------------------------------------
svc_register_with_owner$:
            call    _ir_disable
            push    hl                  ; keep name and fntable below owner:
            push    de                  ; __so_create only consumes the top word
            push    bc                  ; stack owner for __so_create
            ld      de,#SVC_SIZE
            ld      hl,#__svc_first
            call    __so_create         ; de = service or 0; owner left on stack
            pop     bc                  ; drop owner arg
            ld      a,d
            or      e
            jr      z,svr_fail$
            pop     bc                  ; bc = saved fntable
            pop     hl                  ; hl = saved name
            push    bc                  ; preserve fntable across the name copy

            ;; copy name into service->name (bounded, includes the nul)
            push    de                  ; save service
            ld      a,e
            add     a,#SVC_NAME
            ld      e,a
            ld      a,d
            adc     a,#0
            ld      d,a                 ; de = service + SVC_NAME (dest)
            ld      b,#MAX_SVC_NAME_LEN
svr_copy$:
            ld      a,(hl)
            ld      (de),a
            or      a
            jr      z,svr_copied$
            inc     hl
            inc     de
            djnz    svr_copy$
svr_copied$:
            pop     de                  ; de = service
            pop     bc                  ; bc = saved fntable again
            ld      hl,#SVC_FNTABLE
            add     hl,de               ; hl -> service->fntable
            ld      (hl),c
            inc     hl
            ld      (hl),b
            call    _ir_enable
            ret                         ; de = service

svr_fail$:
            pop     bc                  ; discard saved fntable
            pop     hl                  ; discard saved name
            call    _ir_enable
            ret                         ; de = 0

            ;; ----------------------------------------------------------------
            ;; <de> <= _svc_register(<hl> name, <de> fntable)
            ;; ----------------------------------------------------------------
            ;; public service registration binds ownership to the current
            ;; thread so transient user services vanish automatically at owner
            ;; teardown. callers without a current thread fall back to NONE.
            ;; ----------------------------------------------------------------
_svc_register::
            ld      bc,(_thread_current)
            jr      svc_register_with_owner$

            ;; ----------------------------------------------------------------
            ;; <de> <= _svc_register_ownerless(<hl> name, <de> fntable)
            ;; ----------------------------------------------------------------
            ;; internal variant for permanent kernel/OS services.
            ;; ----------------------------------------------------------------
_svc_register_ownerless::
            ld      bc,#0x0000
            jr      svc_register_with_owner$

            ;; ----------------------------------------------------------------
            ;; <de> <= _svc_unregister(<hl> service)
            ;; ----------------------------------------------------------------
_svc_unregister::
            ex      de,hl               ; de = service
            ld      hl,#__svc_first
            jp      __so_destroy

            ;; ----------------------------------------------------------------
            ;; <de> <= _svc_query(<hl> name)
            ;; ----------------------------------------------------------------
            ;; finds the service by name via list_find and returns its function
            ;; table, or 0 if no such service. list_find is caller-clean for its
            ;; two stacked args.
            ;; ----------------------------------------------------------------
_svc_query::
            ;; build __list_find$(first = __svc_first, &prev, svc_match_eq$, name)
            push    iy                  ; preserve the SDCC frame pointer across
                                        ; the callback-driven list walk
            push    hl                  ; prev scratch slot
            ld      b,h
            ld      c,l                 ; bc = name argument
            ld      hl,#0
            add     hl,sp               ; hl = &prev scratch
            ex      de,hl               ; de = &prev
            ld      iy,#svc_match_eq$
            ld      hl,(__svc_first)    ; hl = first element
            call    __list_find$        ; hl = service or 0
            pop     bc                  ; drop prev scratch
            pop     iy
            ld      a,h
            or      l
            jr      nz,svq_found$
            ld      de,#0x0000
            ret
svq_found$:
            ;; hl = service; return service->fntable
            ld      de,#SVC_FNTABLE
            add     hl,de
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = fntable
            ret

            ;; ----------------------------------------------------------------
            ;; __svc_query_rst10()
            ;; ----------------------------------------------------------------
            ;; rst 0x10 syscall bridge: hl = service name -> de = function table.
            ;; ----------------------------------------------------------------
__svc_query_rst10::
            jp      _svc_query

            .area   _INITIALIZED

            ;; head of the service list
__svc_first::
            .dw     0x0000
svc_dead_owner$:
            .dw     0x0000
svc_owner_cleanup_ready$:
            .db     0x00

            .area   _SYSVARS

svc_reg_name$:
            .dw     0x0000
svc_reg_fntable$:
            .dw     0x0000
svc_reg_owner$:
            .dw     0x0000
svc_reg_service$:
            .dw     0x0000

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
            .globl  _svc_unregister
            .globl  _svc_query
            .globl  __svc_query_rst10
            .globl  __svc_first
            .globl  __so_create
            .globl  __so_destroy
            .globl  _list_find

            .equ    SVC_NAME,           4
            .equ    MAX_SVC_NAME_LEN,   16
            .equ    SVC_FNTABLE,        SVC_NAME + MAX_SVC_NAME_LEN
            .equ    SVC_SIZE,           SVC_FNTABLE + 2

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <a> <= svc_match_eq$(<hl> service, <de> name)
            ;; ----------------------------------------------------------------
            ;; list_find callback: returns 1 if the query name equals the
            ;; service's stored name, else 0. bounded by MAX_SVC_NAME_LEN.
            ;; ----------------------------------------------------------------
svc_match_eq$:
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
            ld      a,#1
            ret
sme_false$:
            xor     a
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _svc_register(<hl> name, <de> fntable)
            ;; ----------------------------------------------------------------
            ;; allocates a service sysobj, copies the name and stores the
            ;; function table, then links it into the service list.
            ;; ----------------------------------------------------------------
_svc_register::
            push    de                  ; save fntable (for the end)
            push    hl                  ; save name pointer
            ld      de,#0x0000          ; owner = NONE
            push    de                  ; stack owner for __so_create
            ld      de,#SVC_SIZE
            ld      hl,#__svc_first
            call    __so_create         ; de = service or 0; owner left on stack
            pop     bc                  ; discard owner arg
            pop     hl                  ; hl = name pointer
            ld      a,d
            or      e
            jr      z,svr_fail$

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
            ld      hl,#SVC_FNTABLE
            add     hl,de               ; hl -> service->fntable
            pop     bc                  ; bc = fntable
            ld      (hl),c
            inc     hl
            ld      (hl),b
            ret                         ; de = service

svr_fail$:
            pop     bc                  ; discard saved fntable
            ret                         ; de = 0

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
            ;; build list_find(first = __svc_first, &prev, svc_match_eq$, name)
            push    hl                  ; prev scratch (overwritten by list_find)
            push    hl                  ; arg = name
            ld      hl,#svc_match_eq$
            push    hl                  ; match
            ld      hl,#4
            add     hl,sp               ; hl = &prev scratch
            ex      de,hl               ; de = &prev
            ld      hl,(__svc_first)     ; hl = first element
            call    _list_find          ; de = service or 0
            pop     hl                  ; drop match
            pop     hl                  ; drop arg
            pop     hl                  ; drop prev scratch
            ld      a,d
            or      e
            ret     z                   ; not found -> de = 0
            ;; de = service; return service->fntable
            ld      hl,#SVC_FNTABLE
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

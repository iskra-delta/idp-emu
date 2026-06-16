            ;; sysobj.s
            ;;
            ;; system-owned object helpers layered on top of the kernel heap
            ;; allocator and intrusive list primitives.
            ;;
            ;; native assembly calling convention:
            ;;
            ;;   _so_create:
            ;;     in : hl = &first, de = object size, stack owner
            ;;     out: de = object or 0
            ;;
            ;;   _so_destroy:
            ;;     in : hl = &first, de = object
            ;;     out: de = freed object payload or 0
            ;;
            ;; z80 sdcccall(1) entry points used directly.
            ;; ----------------------------------------------------------------
            ;; 2026-06-14   tstih
            .module sysobj

            .globl  __sys_heap
            .globl  _list_insert
            .globl  _list_remove
            .globl  _mem_allocate
            .globl  _mem_free

            .globl  _so_create
            .globl  _so_destroy

            .equ    SYSOBJ_OWNER,       2

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= _so_create(<hl> **first, <de> size, stack owner)
            ;; ----------------------------------------------------------------
_so_create::
            push    hl
            exx
            pop     hl                  ; hl' = &first
            exx
            ld      hl,#__sys_heap
            call    _mem_allocate
            ld      a,d
            or      e
            ret     z
            pop     af
            pop     bc                  ; bc = owner
            push    bc
            push    af
            push    de
            pop     hl                  ; hl = object
            inc     hl
            inc     hl
            ld      (hl),c
            inc     hl
            ld      (hl),b
            exx
            push    hl
            exx
            pop     hl                  ; hl = &first
            call    _list_insert
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _so_destroy(<hl> **first, <de> object)
            ;; ----------------------------------------------------------------
_so_destroy::
            call    _list_remove
            ld      a,d
            or      e
            ret     z
            ld      hl,#__sys_heap
            jp      _mem_free

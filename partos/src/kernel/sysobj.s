            ;; sysobj.s
            ;;
            ;; tracked object helpers layered on top of the heap allocator and
            ;; intrusive list primitives.
            ;;
            ;; native assembly calling convention:
            ;;
            ;;   __so_create:
            ;;     in : hl = &first, de = object size, stack owner
            ;;     out: de = object or 0
            ;;
            ;;   __so_destroy:
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
            .globl  __mem_allocate$
            .globl  _mem_free

            .globl  __so_create
            .globl  __so_destroy
            .globl  __so_create_on_heap
            .globl  __so_destroy_on_heap
            .globl  _set_cleanup

            ;; owner-death cleanup (merged from owner_cleanup.s): the reason
            ;; system objects exist is owner-bound lifetime, so the cleanup hook
            ;; lives here too.
            .globl  _owner_cleanup_register
            .globl  __owner_cleanup_run

            .equ    SYSOBJ_OWNER,       2
            .equ    OWNER_CLEANUP_MAX,  4

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= __so_create(<hl> **first, <de> size, stack owner)
            ;; ----------------------------------------------------------------
            ;; convenience wrapper that keeps using the shared system heap.
            ;; ----------------------------------------------------------------
__so_create::
            ld      bc,#__sys_heap

            ;; ----------------------------------------------------------------
            ;; <de> <= __so_create_on_heap(<hl> **first, <de> size, <bc> heap,
            ;;                              stack owner)
            ;; ----------------------------------------------------------------
            ;; generic variant used when the caller knows which heap the object
            ;; belongs to. shared/kernel registries keep using __sys_heap, while
            ;; bank-local objects such as events may use the active user heap.
            ;; ----------------------------------------------------------------
__so_create_on_heap::
            push    hl
            exx
            pop     hl                  ; hl' = &first
            exx
            ld      h,b
            ld      l,c                 ; hl = chosen heap
            ;; consume the caller's stacked owner long enough to feed the
            ;; internal allocator register ABI directly. calling the public
            ;; _mem_allocate wrapper from here would treat our caller's return
            ;; address as the owner, because this helper already sits on top
            ;; of the original [return][owner] frame.
            pop     af                  ; af = caller return
            pop     bc                  ; bc = stacked owner
            push    bc                  ; keep owner on caller stack
            push    af                  ; restore our return address
            push    bc                  ; preserve owner across __mem_allocate$
            call    __mem_allocate$
            pop     bc                  ; restore stacked owner
            ex      de,hl
            ld      a,d
            or      e
            ret     z
            push    de
            pop     hl                  ; hl = object
            inc     hl
            inc     hl                  ; hl = object->owner
            ld      (hl),c
            inc     hl
            ld      (hl),b
            exx
            push    hl
            exx
            pop     hl                  ; hl = &first
            jp      _list_insert

            ;; ----------------------------------------------------------------
            ;; <de> <= __so_destroy(<hl> **first, <de> object)
            ;; ----------------------------------------------------------------
__so_destroy::
            ld      bc,#__sys_heap

            ;; ----------------------------------------------------------------
            ;; <de> <= __so_destroy_on_heap(<hl> **first, <de> object,
            ;;                               <bc> heap)
            ;; ----------------------------------------------------------------
            ;; generic destroy helper paired with __so_create_on_heap().
            ;; ----------------------------------------------------------------
__so_destroy_on_heap::
            push    bc                  ; preserve chosen heap across list walk
            call    _list_remove
            pop     bc
            ld      a,d
            or      e
            ret     z
            ld      h,b
            ld      l,c
            jp      _mem_free

            ;; ----------------------------------------------------------------
            ;; _set_cleanup(<hl> obj, <de> cleanup)
            ;; ----------------------------------------------------------------
            ;; sets the object's cleanup hook, called with the object just before
            ;; its memory is freed (cleanup = 0 -> free memory only). the hook is
            ;; stored in the heap block header at obj-2, so it costs the object
            ;; nothing and needs no change to its layout.
            ;; ----------------------------------------------------------------
_set_cleanup::
            dec     hl
            dec     hl                  ; hl = &cleanup (obj-2)
            ld      (hl),e
            inc     hl
            ld      (hl),d
            ret

            ;; ================================================================
            ;; owner-death cleanup (merged from owner_cleanup.s)
            ;; ================================================================
            ;; multiple subsystems may need to purge owner-bound state when an
            ;; owner goes away (fat requests, tty locks, pending async i/o, ...).
            ;; a small callback list keeps the kernel generic. callback ABI:
            ;;   in : de = dead owner pointer ; out: none ; ret: normal.
            ;; ----------------------------------------------------------------

            ;; ----------------------------------------------------------------
            ;; _owner_cleanup_register(<hl> fn)
            ;; ----------------------------------------------------------------
            ;; register one owner-death cleanup callback (duplicates ignored).
            ;; hl = 0 clears the whole table (early bring-up/reset only).
            ;; ----------------------------------------------------------------
_owner_cleanup_register::
            ld      a,h
            or      l
            jr      nz,ocr_add$
            xor     a
            ld      hl,#owner_cleanup_fn$
            ld      b,#(OWNER_CLEANUP_MAX * 2)
ocr_clear$:
            ld      (hl),a
            inc     hl
            djnz    ocr_clear$
            ret
ocr_add$:
            ld      (owner_cleanup_newfn$),hl
            ld      bc,#0x0000          ; first free slot, if any
            ld      de,#owner_cleanup_fn$
            ld      a,#OWNER_CLEANUP_MAX
ocr_scan$:
            push    af
            ld      a,(de)
            ld      l,a
            inc     de
            ld      a,(de)
            ld      h,a
            dec     de
            ld      a,h
            or      l
            jr      z,ocr_free$
            ld      a,(owner_cleanup_newfn$)
            cp      l
            jr      nz,ocr_next$
            ld      a,(owner_cleanup_newfn$ + 1)
            cp      h
            jr      z,ocr_done$
ocr_next$:
            inc     de
            inc     de
            pop     af
            dec     a
            jr      nz,ocr_scan$
            ret                         ; table full -> ignore
ocr_free$:
            ld      a,b
            or      c
            jr      nz,ocr_keep_free$
            ex      de,hl               ; hl = free slot
            ld      b,h
            ld      c,l                 ; bc = first free slot
            ex      de,hl               ; de = scan ptr
ocr_keep_free$:
            inc     de
            inc     de
            pop     af
            dec     a
            jr      nz,ocr_scan$
            ld      a,b
            or      c
            ret     z
            ld      h,b
            ld      l,c
            ld      a,(owner_cleanup_newfn$)
            ld      (hl),a
            inc     hl
            ld      a,(owner_cleanup_newfn$ + 1)
            ld      (hl),a
ocr_done$:
            ret

            ;; ----------------------------------------------------------------
            ;; __owner_cleanup_run(<de> owner)
            ;; ----------------------------------------------------------------
            ;; run every registered callback with de = owner. internal asm ABI.
            ;; ----------------------------------------------------------------
__owner_cleanup_run::
            ld      hl,#owner_cleanup_fn$
            ld      a,#OWNER_CLEANUP_MAX
ocr_run$:
            push    af
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            inc     hl
            ld      a,b
            or      c
            jr      z,ocr_skip$
            push    de
            push    hl
            ld      h,b
            ld      l,c
            ld      bc,#ocr_return$
            push    bc
            jp      (hl)
ocr_return$:
            pop     hl
            pop     de
ocr_skip$:
            pop     af
            dec     a
            jr      nz,ocr_run$
            ret

            .area   _SYSVARS

owner_cleanup_fn$:
            .ds     OWNER_CLEANUP_MAX * 2
owner_cleanup_newfn$:
            .dw     0x0000

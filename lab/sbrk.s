            ;; sbrk.s
            ;;
            ;; heap break allocator
            ;;
            ;; 2026-06-05   tstih
            .module sbrk

            .include "../../partos.inc"

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; sbrk_init()
            ;; ----------------------------------------------------------------
            ;; initializes the heap break to HEAP_TOP (highest free byte).
            ;; must be called once before any sbrk() call.
            ;;
            ;; destroys:
            ;;  hl  ... HEAP_TOP
            ;; ----------------------------------------------------------------
sbrk_init::
            ld      hl,#HEAP_TOP
            ld      (heap_brk),hl
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> *block <= sbrk(<de> size)
            ;; ----------------------------------------------------------------
            ;; grows the heap downward by size bytes and returns a pointer to
            ;; the start (lowest byte) of the newly allocated block. size must
            ;; be nonzero. there is no free: the heap only grows.
            ;; the heap break is a shared (singleton) structure; serialize
            ;; calls (ir.s/lock.s) when used outside single-threaded boot.
            ;;
            ;; input(s):
            ;;  de  ... bytes to allocate (nonzero)
            ;; output(s):
            ;;  hl  ... pointer to allocated block, 0x0000 if out of memory
            ;;  flags   ... Z on success, NZ if out of memory
            ;; destroys:
            ;;  a   ... 0 on success, 0xff if out of memory
            ;;  bc  ... HEAP_BOTTOM-1 (preserved on early underflow)
            ;; ----------------------------------------------------------------
sbrk::
            ld      hl,(heap_brk)       ; hl = highest free byte
            or      a                   ; clear carry
            sbc     hl,de               ; hl = new break (grows down)
            jr      c,sbrk_oom$         ; oom: size larger than break
            ld      bc,#HEAP_BOTTOM-1   ; block start (hl+1) must be >= bottom
            push    hl                  ; save new break
            or      a                   ; clear carry
            sbc     hl,bc               ; carry if new break < HEAP_BOTTOM-1
            pop     hl                  ; restore new break
            jr      c,sbrk_oom$         ; oom: would underflow heap bottom
            ld      (heap_brk),hl       ; commit new break
            inc     hl                  ; hl = start of allocated block
            xor     a                   ; Z = success
            ret
sbrk_oom$:
            ld      hl,#0x0000          ; return NULL
            ld      a,#0xff
            or      a                   ; NZ = out of memory
            ret

            .area   _SYSVARS
heap_brk::
            .ds     2                   ; highest free heap byte

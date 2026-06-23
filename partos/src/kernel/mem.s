            ;; mem.s
            ;;
            ;; yos-style heap allocator for the kernel's system heap.
            ;; block headers are intrusive sys objects so all heap chunks live
            ;; in one forward list and are tracked with list.s helpers.
            ;;
            ;; internal helper calling convention:
            ;;
            ;;   _mem_init:
            ;;     in : hl = heap start, de = heap size
            ;;
            ;;   __mem_allocate$:
            ;;     in : hl = heap start, de = payload size, bc = owner
            ;;     out: hl = payload pointer or 0
            ;;
            ;;   __mem_free$:
            ;;     in : hl = heap start, de = payload pointer
            ;;     out: hl = payload pointer of resulting free block or 0
            ;;
            ;;   __mem_free_owner:
            ;;     in : hl = heap start, de = owner
            ;;     out: a = number of blocks freed
            ;;
            ;; public c entry points:
            ;;   _mem_* use z80 sdcccall(1)
            ;; ----------------------------------------------------------------
            ;; 2026-06-14   tstih
            .module mem

            .globl  _mem_init
            .globl  _mem_allocate
            .globl  _mem_free
            .globl  __mem_free_owner
            .globl  _ir_disable
            .globl  _ir_enable

            .equ    NONE,               0x0000
            .equ    MIN_CHUNK_SIZE,     4
            .equ    NEW,                0x00
            .equ    ALLOCATED,          0x01

            .equ    SYSOBJ_NEXT,        0
            .equ    SYSOBJ_OWNER,       2

            .equ    BLOCK_STAT,         4
            .equ    BLOCK_SIZE,         5
            ;; per-object cleanup hook, called before the block is freed (0 =
            ;; none, free memory only). sits just before the payload (payload-2).
            .equ    BLOCK_DTOR,         7
            .equ    BLOCK_DATA,         9
            .equ    BLK_SIZE,           BLOCK_DATA
            .equ    SPLIT_THRESHOLD,    BLK_SIZE + MIN_CHUNK_SIZE + 1

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; mem_merge_with_next$(<hl> block)
            ;; ----------------------------------------------------------------
            ;; merges block with its immediate next block. caller guarantees
            ;; that next exists and is free.
            ;; ----------------------------------------------------------------
mem_merge_with_next$:
            push    hl                  ; save block
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl               ; hl = next block
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = next->next
            inc     hl
            inc     hl
            inc     hl
            inc     hl                  ; hl = next->size
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = next->size
            ld      a,c
            add     a,#BLK_SIZE
            ld      c,a
            jr      nc,mmn_no_carry$
            inc     b
mmn_no_carry$:
            pop     hl                  ; hl = block
            push    hl                  ; preserve return block
            ld      (hl),e
            inc     hl
            ld      (hl),d              ; block->next = next->next
            inc     hl
            inc     hl
            inc     hl
            inc     hl                  ; hl = block->size
            ld      a,(hl)
            add     a,c
            ld      (hl),a
            inc     hl
            ld      a,(hl)
            adc     a,b
            ld      (hl),a
            pop     hl
            ret

            ;; ----------------------------------------------------------------
            ;; mem_split$(<hl> block, <de> size)
            ;; ----------------------------------------------------------------
            ;; splits one free block into an allocated-sized prefix and a new
            ;; free successor block.
            ;; ----------------------------------------------------------------
mem_split$:
            push    de                  ; save requested size
            push    hl                  ; save block
            add     hl,de
            ld      bc,#BLOCK_DATA
            add     hl,bc               ; hl = new block
            push    hl
            pop     de                  ; de = new block
            pop     hl                  ; hl = block

            ld      bc,#BLOCK_STAT + 1
            ldir                        ; copy next, owner and stat

            pop     bc                  ; bc = requested size
            push    bc                  ; keep requested size
            ld      a,c
            add     a,#BLK_SIZE
            ld      c,a
            jr      nc,ms_no_carry$
            inc     b
ms_no_carry$:
            ld      a,(hl)
            sub     c
            ld      c,a
            inc     hl
            ld      a,(hl)
            sbc     a,b
            ld      b,a                 ; bc = new block size
            ld      a,c
            ld      (de),a
            inc     de
            ld      a,b
            ld      (de),a

            ld      a,e
            sub     #6
            ld      c,a
            ld      a,d
            sbc     a,#0
            ld      b,a                 ; bc = new block pointer

            pop     de                  ; de = requested size
            ld      a,d
            ld      (hl),a
            dec     hl
            ld      a,e
            ld      (hl),a              ; block->size = requested size
            dec     hl
            dec     hl
            dec     hl
            dec     hl
            dec     hl                  ; hl = block
            ld      a,c
            ld      (hl),a
            inc     hl
            ld      a,b
            ld      (hl),a              ; block->next = new block
            ret

            ;; ----------------------------------------------------------------
            ;; _mem_init(<hl> heap, <de> size)
            ;; ----------------------------------------------------------------
_mem_init::
            call    _ir_disable
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      a,e
            sub     #BLK_SIZE
            ld      (hl),a
            inc     hl
            ld      a,d
            sbc     a,#0
            ld      (hl),a
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> <= __mem_allocate$(<hl> heap, <de> size, <bc> owner)
            ;; ----------------------------------------------------------------
__mem_allocate$:
            push    bc
            exx
            pop     bc                  ; bc' = owner
            exx

ma_scan$:
            push    hl
            ld      bc,#BLOCK_STAT
            add     hl,bc
            ld      a,(hl)
            and     #ALLOCATED
            jr      nz,ma_next$
            inc     hl
            ld      a,(hl)
            sub     e
            inc     hl
            ld      a,(hl)
            sbc     a,d
            jr      nc,ma_found$

ma_next$:
            pop     hl
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      h,b
            ld      l,c
            ld      a,h
            or      l
            jr      nz,ma_scan$
            ld      hl,#0x0000
            ret

ma_found$:
            pop     hl
            push    hl
            ld      bc,#BLOCK_SIZE
            add     hl,bc
            ld      a,(hl)
            sub     e
            ld      c,a
            inc     hl
            ld      a,(hl)
            sbc     a,d
            ld      b,a                 ; bc = remaining bytes
            pop     hl

            ld      a,b
            or      a
            jr      nz,ma_split$
            ld      a,c
            cp      #SPLIT_THRESHOLD
            jr      c,ma_mark$

ma_split$:
            push    hl
            call    mem_split$
            pop     hl

ma_mark$:
            inc     hl
            inc     hl                  ; hl = block->owner
            exx
            ld      a,c
            exx
            ld      (hl),a
            inc     hl
            exx
            ld      a,b
            exx
            ld      (hl),a
            inc     hl                  ; hl = block->stat
            ld      (hl),#ALLOCATED
            inc     hl
            inc     hl
            inc     hl                  ; hl = block->dtor (block+7)
            xor     a
            ld      (hl),a              ; dtor = 0 (free memory only by default)
            inc     hl
            ld      (hl),a
            inc     hl                  ; hl = block->data (block+9)
            ret

_mem_allocate::
            call    _ir_disable
            pop     af
            pop     bc
            push    bc
            push    af
            call    __mem_allocate$
            ex      de,hl
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <hl> <= __mem_free$(<hl> heap, <de> payload)
            ;; ----------------------------------------------------------------
            ;; ----------------------------------------------------------------
            ;; mem_run_dtor$(<hl> payload)
            ;; ----------------------------------------------------------------
            ;; calls the block's cleanup hook stored at payload-2 with hl =
            ;; payload, if it is non-zero. tail-calls the hook; returns to the
            ;; caller of mem_run_dtor$ either way (no hook -> immediate ret).
            ;; ----------------------------------------------------------------
mem_run_dtor$:
            dec     hl
            dec     hl                  ; hl = &cleanup (payload-2)
            ld      e,(hl)
            inc     hl
            ld      d,(hl)              ; de = cleanup hook
            inc     hl                  ; hl = payload
            ld      a,d
            or      e
            ret     z                   ; no hook -> nothing to do
            ld      bc,#mrd_ret$
            push    bc                  ; return address for the hook
            push    de
            ret                         ; jp (hook) with hl = payload
mrd_ret$:
            ret

__mem_free$:
            ;; run the object's cleanup hook first; memory is released last.
            push    hl                  ; save heap
            push    de                  ; save payload
            ex      de,hl               ; hl = payload
            call    mem_run_dtor$
            pop     de                  ; de = payload
            pop     hl                  ; hl = heap
            ld      a,e
            sub     #BLOCK_DATA
            ld      e,a
            ld      a,d
            sbc     a,#0
            ld      d,a                 ; de = target block
            ld      bc,#0x0000          ; bc = prev block

mf_scan$:
            ld      a,h
            or      l
            jr      z,mf_fail$
            ld      a,l
            cp      e
            jr      nz,mf_next$
            ld      a,h
            cp      d
            jr      z,mf_found$

mf_next$:
            ld      b,h
            ld      c,l
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      mf_scan$

mf_found$:
            push    hl
            inc     hl
            inc     hl                  ; hl = block->owner
            xor     a
            ld      (hl),a
            inc     hl
            ld      (hl),a
            inc     hl
            ld      (hl),a              ; block->stat = NEW
            pop     hl

            ld      a,b
            or      c
            jr      z,mf_try_next$
            push    hl
            ld      h,b
            ld      l,c                 ; hl = prev
            push    hl
            ld      de,#BLOCK_STAT
            add     hl,de
            ld      a,(hl)
            and     #ALLOCATED
            pop     hl                  ; hl = prev
            jr      nz,mf_prev_alloc$
            call    mem_merge_with_next$
            pop     de                  ; drop original block
            jr      mf_try_next$

mf_prev_alloc$:
            pop     hl                  ; restore original block

mf_try_next$:
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            dec     hl                  ; hl = current block
            ld      a,b
            or      c
            jr      z,mf_return$
            push    hl
            ld      h,b
            ld      l,c                 ; hl = next
            ld      de,#BLOCK_STAT
            add     hl,de
            ld      a,(hl)
            and     #ALLOCATED
            pop     hl
            jr      nz,mf_return$
            call    mem_merge_with_next$

mf_return$:
            ld      de,#BLOCK_DATA
            add     hl,de
            ret

mf_fail$:
            ld      hl,#0x0000
            ret

_mem_free::
            call    _ir_disable
            call    __mem_free$
            ex      de,hl
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= __mem_free_owner(<hl> heap, <de> owner)
            ;; ----------------------------------------------------------------
__mem_free_owner::
            call    _ir_disable
            push    hl                  ; saved heap
            push    de                  ; saved owner
            xor     a
            ld      h,a
            ld      l,a
            push    hl                  ; saved freed count (low byte returned)
            ld      b,d
            ld      c,e                 ; bc = owner

mfo_restart$:
            ld      hl,#4
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl               ; hl = heap

mfo_scan$:
            ld      a,h
            or      l
            jr      z,mfo_done$
            push    hl
            ld      de,#BLOCK_STAT
            add     hl,de
            ld      a,(hl)
            and     #ALLOCATED
            jr      z,mfo_next$
            dec     hl
            dec     hl                  ; hl = block->owner
            ld      a,(hl)
            cp      c
            jr      nz,mfo_next$
            inc     hl
            ld      a,(hl)
            cp      b
            jr      nz,mfo_next$
            pop     de                  ; de = block
            ex      de,hl
            ld      de,#BLOCK_DATA
            add     hl,de
            ex      de,hl               ; de = payload
            push    bc
            push    de                  ; keep payload while we reload heap
            ld      hl,#8
            add     hl,sp
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            ex      de,hl               ; hl = heap
            pop     de                  ; de = payload
            call    __mem_free$
            pop     bc
            ld      hl,#0
            add     hl,sp               ; hl = saved count
            inc     (hl)
            jr      nz,mfo_restart$
            inc     hl
            inc     (hl)
            jr      mfo_restart$

mfo_next$:
            pop     hl
            ld      a,(hl)
            inc     hl
            ld      h,(hl)
            ld      l,a
            jr      mfo_scan$

mfo_done$:
            pop     hl                  ; hl = saved count
            ld      a,l
            pop     de                  ; drop saved owner
            pop     hl                  ; drop saved heap
            jp      _ir_enable

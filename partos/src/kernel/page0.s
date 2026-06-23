            ;; page0.s
            ;;
            ;; PartOS micro-kernel low page. in the split build the micro-kernel
            ;; is linked at 0x0000, so this is no longer a template copied into
            ;; page 0 -- it IS the first thing in kernel _CODE and therefore sits
            ;; at the rst/nmi entry addresses directly.
            ;;
            ;; each hardware rst/nmi slot holds a DIRECT `jp <handler>`. because
            ;; the slot sits at a fixed address (0x08, 0x10, ... 0x38, nmi 0x66),
            ;; set_vector(slot, handler) simply patches the 2-byte operand of that
            ;; jp in place -- so there is NO separate vector table: the slot IS the
            ;; table. the "vector number" passed to set_vector is literally the
            ;; slot address (all < 0x100, fits a byte). default targets:
            ;;   rst 0x08  -> __kernel_api  (returns hl = &_kernel_table)
            ;;   rst 0x10..0x30, all -> __sys_rst_default  (ret)
            ;;   rst 0x38 (im 1) -> __sys_rst38_default (reti)
            ;;   nmi 0x66        -> __sys_nmi_default  (retn)
            ;; rst 0x00 is the cold entry: `di ; jp __sys_kernel` (no vectoring).
            ;;
            ;; consequence: the low page is now SELF-MODIFIED by set_vector (it is
            ;; not read-only after boot), and once banking lands set_vector must
            ;; patch the operand in BOTH bank mirrors. set_vector is cold, so this
            ;; is cheap. the soft 50 Hz TICK hook is NOT here -- it is reached by a
            ;; software call from the VBL ISR, not a rst, so it stays an indirect
            ;; cell (see __tick_dispatch / __sys_vec_tick in vectors.s).
            ;;
            ;; this module MUST be linked FIRST in _CODE so __sys_page0 lands at
            ;; 0x0000 and the rst slots fall on their fixed 8-byte boundaries.
            ;;
            ;; 2026-06-21   tstih
            .module page0

            .include "../partos.inc"        ; BANK_*_PORT for the bank copies

            .globl  __sys_page0
            .globl  __sys_page0_end
            .globl  __sys_kernel
            .globl  __kernel_api
            .globl  __thread_robin
            .globl  __sys_rst_default
            .globl  __sys_rst38_default
            .globl  __sys_nmi_default
            ;; the one bank block-copy routine -- it lives in the dead bytes
            ;; after the rst38 jump + the rst38->nmi pad, so it costs no extra
            ;; space, and being a plain imm-port loop with no self-modification it
            ;; runs unchanged from this read-only mirrored low page (or relocated
            ;; to exec space at boot).
            .globl  __bank_copy
            .globl  __bank_copy_size
            .globl  __boot_version_hint
            .globl  __boot_model_hint
            .globl  __boot_flags_hint
            .globl  __boot_meta1_hint
            ;; scheduler list heads parked in the dead pre-nmi pad (see below).
            .globl  _thread_current
            .globl  _thread_first_suspended
            .globl  _thread_first_running
            .globl  _thread_first_waiting
            .globl  _thread_first_terminated
            .globl  __evt_first

            ;; ----------------------------------------------------------------
            ;; micro-kernel low page (linked at 0x0000)
            ;; ----------------------------------------------------------------
            .area   _CODE
__sys_page0::

            ;; ----------------------------------------------------------------
            ;; rst 0x00 -- reset / cold entry
            ;; ----------------------------------------------------------------
            ;; bytes 0x0002-0x0003 are the operand of this jp, so 0x0004..0x0007
            ;; stay free for the kernel identity bytes below.
            ;; ----------------------------------------------------------------
            di
            jp      __sys_kernel

            ;; 0x04-0x07: ROM-written boot hints. rst 0x00 is di+jp (4 bytes),
            ;; so these four bytes are unreachable as code and only exist to
            ;; push rst 0x08 onto its boundary. the ROM fills them after it has
            ;; loaded the split images:
            ;;   +0 version hint
            ;;   +1 model hint
            ;;   +2 flags hint
            ;;   +3 meta1 hint (currently boot-device id)
__boot_version_hint::
            .db     0
__boot_model_hint::
            .db     0
__boot_flags_hint::
            .db     0
__boot_meta1_hint::
            .db     0

            ;; ----------------------------------------------------------------
            ;; rst 0x08 -- kernel ABI discovery
            ;; ----------------------------------------------------------------
            ;; default target __kernel_api, which returns hl = &_kernel_table.
            ;; ----------------------------------------------------------------
            jp      __kernel_api
            .db     0, 0, 0, 0, 0

            ;; ----------------------------------------------------------------
            ;; rst 0x10
            ;; ----------------------------------------------------------------
            ;; unassigned; default target __sys_rst_default (ret).
            ;; ----------------------------------------------------------------
            jp      __sys_rst_default
            .db     0, 0, 0, 0, 0

            ;; ----------------------------------------------------------------
            ;; rst 0x18 -- scheduler tick / yield -> context switch
            ;; ----------------------------------------------------------------
            ;; the kernel never fires this itself: a thread issues `rst 0x18` to
            ;; yield, and the OS routes a timer interrupt here to preempt. no
            ;; source wired -> cooperative (single-tasking); a timer wired ->
            ;; preemptive.
            ;; ----------------------------------------------------------------
            jp      __thread_robin
            .db     0, 0, 0, 0, 0

            ;; ----------------------------------------------------------------
            ;; rst 0x20
            ;; ----------------------------------------------------------------
            ;; unassigned; default target __sys_rst_default (ret).
            ;; ----------------------------------------------------------------
            jp      __sys_rst_default
            .db     0, 0, 0, 0, 0

            ;; ----------------------------------------------------------------
            ;; rst 0x28
            ;; ----------------------------------------------------------------
            ;; unassigned; default target __sys_rst_default (ret).
            ;; ----------------------------------------------------------------
            jp      __sys_rst_default
            .db     0, 0, 0, 0, 0

            ;; ----------------------------------------------------------------
            ;; rst 0x30
            ;; ----------------------------------------------------------------
            ;; unassigned; default target __sys_rst_default (ret).
            ;; ----------------------------------------------------------------
            jp      __sys_rst_default
            .db     0, 0, 0, 0, 0

            ;; ----------------------------------------------------------------
            ;; rst 0x38 -- im 1 interrupt entry
            ;; ----------------------------------------------------------------
            ;; default target __sys_rst38_default (reti).
            ;; ----------------------------------------------------------------
            jp      __sys_rst38_default

            ;; ----------------------------------------------------------------
            ;; __bank_copy(<hl> src, <de> dst, <bc> count, <a> dir)
            ;; ----------------------------------------------------------------
            ;; the bytes after the rst38 jump are unreachable by rst (the cpu only
            ;; generates rst 0x00..0x38), so the ONE bank block-copy lives here,
            ;; flowing into the old rst38->nmi pad -- it costs no space.
            ;;
            ;; generic byte copy from <src> in the CURRENT bank to <dst> in the
            ;; OTHER bank (src/dst independent); each byte rides in A across the
            ;; flip (bank select is value-independent). direction picks the order
            ;; of the two bank ports: dir == 0 -> current A -> B, dir != 0 -> B -> A.
            ;; fixed-port immediates (no self-modification), so it runs unchanged
            ;; from this read-only mirrored low page, or relocated to exec space
            ;; for the boot mirror (while bank B's low page does not exist yet).
            ;;
            ;; INTERRUPTS MUST BE OFF (an isr mid-flip would run in the dst bank).
            ;; destroys: a, bc, de, hl, flags
            ;; ----------------------------------------------------------------
__bank_copy::
            or      a
            jr      nz,bc_b2a$
bc_a2b$:
            ld      a,(hl)              ; read src (current = A)
            out     (BANK_B_PORT),a     ; -> B
            ld      (de),a              ; write dst in B
            out     (BANK_A_PORT),a     ; restore A
            inc     hl
            inc     de
            dec     bc
            ld      a,b
            or      c
            jr      nz,bc_a2b$
            ret
bc_b2a$:
            ld      a,(hl)              ; read src (current = B)
            out     (BANK_A_PORT),a     ; -> A
            ld      (de),a              ; write dst in A
            out     (BANK_B_PORT),a     ; restore B
            inc     hl
            inc     de
            dec     bc
            ld      a,b
            or      c
            jr      nz,bc_b2a$
            ret
__bank_copy_end::
            __bank_copy_size == __bank_copy_end - __bank_copy

            ;; ----------------------------------------------------------------
            ;; scheduler list heads -- parked in the dead pre-nmi pad
            ;; ----------------------------------------------------------------
            ;; the 6 list heads are exactly 12 bytes (6 words) and fill the gap
            ;; from here to the nmi entry at 0x66. they are .dw 0 (loaded zero,
            ;; no runtime init), and live here purely to keep _INITIALIZED under
            ;; the 2 KB mirror line by reusing space we already pay for.
            ;; NOTE: in the mirrored low page these are PER-BANK copies -- correct
            ;; for single-bank, but cross-bank scheduling must relocate them to a
            ;; single shared-RAM copy when banking goes live.
            ;; ----------------------------------------------------------------
_thread_current::
            .dw     0x0000
_thread_first_suspended::
            .dw     0x0000
_thread_first_running::
            .dw     0x0000
_thread_first_waiting::
            .dw     0x0000
_thread_first_terminated::
            .dw     0x0000
__evt_first::
            .dw     0x0000
            ;; guard: the 6 words fill 0x5a..0x65 exactly; pad any slack to nmi.
            .ds     0x66 - (. - __sys_page0)

            ;; ----------------------------------------------------------------
            ;; nmi -- 0x66
            ;; ----------------------------------------------------------------
            ;; default target __sys_nmi_default (retn).
            ;; ----------------------------------------------------------------
            jp      __sys_nmi_default
__sys_page0_end::

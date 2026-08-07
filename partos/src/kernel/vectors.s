            ;; vectors.s
            ;;
            ;; low-page vector management. the hardware rst/nmi slots live in the
            ;; low page (page0.s) as direct `jp <handler>` instructions at fixed
            ;; addresses, so there is no vector table: set_vector patches the jp
            ;; operand in place and the "vector" passed in is the slot ADDRESS
            ;; itself (0x08, 0x10, ... 0x38, nmi 0x66 -- all < 0x100, so a byte).
            ;;
            ;; the 50 Hz TICK hook is the one exception: it is not a rst, it is a
            ;; soft hook the VBL ISR reaches by a software `call`, so it keeps a
            ;; single indirect cell (__sys_vec_tick) dispatched by __tick_dispatch.
            ;;
            ;; 2026-06-14   tstih
            .module vectors

            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _vector_set
            .globl  _vector_get
            .globl  __tick_dispatch
            .globl  __sys_vec_tick
            .globl  __sys_rst_default
            .globl  __sys_rst38_default
            .globl  __sys_nmi_default

            ;; public vector ids == low-page slot addresses (see page0.s).
            .equ    VECTOR_RST08,      0x08
            .equ    VECTOR_RST10,      0x10
            .equ    VECTOR_RST18,      0x18
            .equ    VECTOR_RST20,      0x20
            .equ    VECTOR_RST28,      0x28
            .equ    VECTOR_RST30,      0x30
            .equ    VECTOR_RST38,      0x38
            .equ    VECTOR_NMI,        0x66

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; default vector targets (jp'd to from the low-page slots)
            ;; ----------------------------------------------------------------
__sys_rst_default::
            ret

__sys_rst38_default::
            ei
            reti

__sys_nmi_default::
            retn

            ;; ----------------------------------------------------------------
            ;; __tick_dispatch() -- call the registered 50 hz tick hook
            ;; ----------------------------------------------------------------
            ;; the scheduler's vbl handler calls here once per tick (inside the
            ;; ir bracket). it forwards to whatever handler is installed in the
            ;; __sys_vec_tick cell and returns when that handler returns. the
            ;; default is a no-op `ret`, so with no driver registered the tick
            ;; costs one indirect jump. a soft-timer driver (e.g. the ctc driver)
            ;; claims it by storing its chain routine into __sys_vec_tick.
            ;; ----------------------------------------------------------------
__tick_dispatch::
            ld      hl,(__sys_vec_tick)
            jp      (hl)                ; tail-call hook; its ret resumes caller

            ;; ----------------------------------------------------------------
            ;; _vector_set(<a> slot, <de> handler)
            ;; ----------------------------------------------------------------
            ;; <a> is the low-page slot address (0x08..0x38, 0x66); the slot holds
            ;; a `jp` whose operand is patched here. no return value -- use
            ;; _vector_get to read a slot's current handler.
            ;; ----------------------------------------------------------------
_vector_set::
            ld      c,a
            call    _ir_disable
            ld      l,c
            ld      h,#0x00             ; hl = slot (jp opcode address)
            inc     hl                  ; hl = slot+1 (operand low byte)
            ld      (hl),e              ; new low
            inc     hl                  ; hl = slot+2 (operand high byte)
            ld      (hl),d              ; new high
            jp      _ir_enable

            ;; ----------------------------------------------------------------
            ;; <de> <= _vector_get(<a> slot)
            ;; ----------------------------------------------------------------
            ;; returns the handler currently stored in the slot's jp operand.
            ;; ----------------------------------------------------------------
_vector_get::
            call    _ir_disable
            ld      l,a
            ld      h,#0x00
            inc     hl                  ; slot+1
            ld      e,(hl)
            inc     hl                  ; slot+2
            ld      d,(hl)              ; de = handler
            jp      _ir_enable

            .area   _CODE

__sys_vec_tick::
            .dw     __sys_rst_default       ; no-op until a tick driver registers

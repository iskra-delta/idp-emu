            ;; vectors.s
            ;;
            ;; shared-memory kernel vector dispatch table.
            ;; the copied low-page image loads handler addresses directly from
            ;; this table with `ld hl,(#vector)` / `jp (hl)`. only the reset
            ;; entry stub remains in shared memory because rst 0x00 must leave
            ;; bytes 0x04..0x07 available for page-0 metadata.
            ;;
            ;; 2026-06-14   tstih
            .module vectors

            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _vector_get
            .globl  _vector_set
            .globl  __sys_entry
            .globl  __sys_kernel
            .globl  __sys_vec_entry
            .globl  __sys_vec_rst08
            .globl  __sys_vec_rst10
            .globl  __sys_vec_rst18
            .globl  __sys_vec_rst20
            .globl  __sys_vec_rst28
            .globl  __sys_vec_rst30
            .globl  __sys_vec_rst38
            .globl  __sys_vec_nmi

            .equ    VECTOR_ENTRY,      0
            .equ    VECTOR_RST08,      1
            .equ    VECTOR_RST10,      2
            .equ    VECTOR_RST18,      3
            .equ    VECTOR_RST20,      4
            .equ    VECTOR_RST28,      5
            .equ    VECTOR_RST30,      6
            .equ    VECTOR_RST38,      7
            .equ    VECTOR_NMI,        8
            .equ    VECTOR_COUNT,      9

            .area   _CODE

__sys_entry::
            ld      hl,(#__sys_vec_entry)
            jp      (hl)

__sys_rst_default$:
            ret

__sys_rst38_default$:
            reti

__sys_nmi_default$:
            retn

            ;; ----------------------------------------------------------------
            ;; <hl> <= __vector_ptr$(<a> vector index)
            ;; ----------------------------------------------------------------
            ;; resolves one public vector index to its shared-memory table
            ;; cell. returns 0 on an invalid index.
            ;; ----------------------------------------------------------------
__vector_ptr$:
            cp      #VECTOR_COUNT
            jr      nc,vp_invalid$
            ld      l,a
            ld      h,#0x00
            add     hl,hl
            ld      de,#__sys_vec_entry
            add     hl,de
            ret

vp_invalid$:
            ld      hl,#0x0000
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _vector_get(<a> vector)
            ;; ----------------------------------------------------------------
            ;; returns the current handler stored in the shared-memory vector
            ;; table, or 0 on an invalid vector index.
            ;; ----------------------------------------------------------------
_vector_get::
            ld      c,a
            call    _ir_disable
            ld      a,c
            call    __vector_ptr$
            ld      a,h
            or      l
            jr      z,vg_invalid$
            ld      e,(hl)
            inc     hl
            ld      d,(hl)
            call    _ir_enable
            ret

vg_invalid$:
            ld      de,#0x0000
            call    _ir_enable
            ret

            ;; ----------------------------------------------------------------
            ;; <de> <= _vector_set(<a> vector, <de> handler)
            ;; ----------------------------------------------------------------
            ;; stores a new handler into the shared-memory vector table and
            ;; returns the previous handler, or 0 on an invalid vector index.
            ;; ----------------------------------------------------------------
_vector_set::
            ld      c,a
            call    _ir_disable
            ld      a,c
            call    __vector_ptr$
            ld      a,h
            or      l
            jr      z,vs_invalid$
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = previous handler
            ld      (hl),d
            dec     hl
            ld      (hl),e
            ld      e,c
            ld      d,b
            call    _ir_enable
            ret

vs_invalid$:
            ld      de,#0x0000
            call    _ir_enable
            ret

            .area   _INITIALIZED

__sys_vec_entry::
            .dw     __sys_kernel
__sys_vec_rst08::
            .dw     __sys_rst_default$
__sys_vec_rst10::
            .dw     __sys_rst_default$
__sys_vec_rst18::
            .dw     __sys_rst_default$
__sys_vec_rst20::
            .dw     __sys_rst_default$
__sys_vec_rst28::
            .dw     __sys_rst_default$
__sys_vec_rst30::
            .dw     __sys_rst_default$
__sys_vec_rst38::
            .dw     __sys_rst38_default$
__sys_vec_nmi::
            .dw     __sys_nmi_default$

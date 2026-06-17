            ;; ctc.inc
            ;;
            ;; partner z80 ctc (counter/timer circuit) constants. one i/o port
            ;; per channel, 0xc8..0xcb. must match include/ctc.h.
            ;;
            ;; 2026-06-16   tstih

            ;; --- channel i/o ports -------------------------------------------
            .equ    CTC_CH0,        0xc8 ; system timer (cascades to ch1)
            .equ    CTC_CH1,        0xc9 ; counter fed by ch0 (fdc motor etc.)
            .equ    CTC_CH2,        0xca
            .equ    CTC_CH3,        0xcb ; avdc vertical blank (vbl) -> tick

            ;; --- control word bits (written when bit0 = 1) -------------------
            .equ    CTC_CW,         0x01 ; bit0: 1=control word, 0=int vector
            .equ    CTC_RESET,      0x02 ; bit1: software reset
            .equ    CTC_TC,         0x04 ; bit2: time-constant byte follows
            .equ    CTC_TRIGGER,    0x08 ; bit3: (timer) start on clk/trg edge
            .equ    CTC_RISING,     0x10 ; bit4: active edge 1=rising 0=falling
            .equ    CTC_PRE256,     0x20 ; bit5: (timer) prescale 1=/256 0=/16
            .equ    CTC_COUNTER,    0x40 ; bit6: 1=counter mode, 0=timer mode
            .equ    CTC_INT,        0x80 ; bit7: interrupt enable

            ;; --- im2 vector base ---------------------------------------------
            ;; low byte of the vector; the high byte is the i register (im 2
            ;; page). channels emit base + 2*channel:
            ;;   ch0 = base+0  ch1 = base+2  ch2 = base+4  ch3 = base+6 (vbl)
            ;; base must be a multiple of 8 (low 3 bits zero; bit0 = 0 also
            ;; marks the write to ch0 as a vector, not a control word).
            .equ    CTC_VEC_BASE,   0x88 ; ch3 (vbl) vector = 0x8e
            .equ    CTC_VEC_VBL,    CTC_VEC_BASE + 6 ; ch3 vertical-blank vector

            ;; --- ch3 (vbl) time constant -------------------------------------
            ;; number of vertical blanks per interrupt; 1 = every frame (~50 hz).
            .equ    CTC_VBL_DIV,    0x01

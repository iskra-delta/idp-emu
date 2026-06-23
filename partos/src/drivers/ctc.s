            ;; ctc.s
            ;;
            ;; partner z80 ctc driver. the ctc is a system timer chip rather
            ;; than a stream device, so the only meaningful entry point is the
            ;; chip initialization (ctc_init / ctc_probe). read/write/open/close
            ;; are intentionally left unimplemented for now and resolve to the
            ;; shared "unsupported" stubs.
            ;;
            ;; the init sequence follows the original partner rom idiom (see
            ;; docs/roms/rom-gdp-anno.txt around 0x056e): software-reset the
            ;; channels, program the interrupt vector base through channel 0,
            ;; then write each channel's control word + time constant. the rom
            ;; used a ch0->ch1 cascade counter for its cp/m tick; partos instead
            ;; arms channel 3, which is clocked by the avdc vertical-blank line
            ;; (the ~50 hz "vbl") and is the natural preemption/scheduler tick.
            ;;
            ;; 2026-06-16   tstih
            .module ctc

            .include "dev.inc"
            .include "drv.inc"
            .include "ctc.inc"

            .globl  ctc_init
            .globl  ctc_enable_tick
            .globl  ctc_dev
            .globl  sio_dev_drv
            .globl  drv_read_unsupported
            .globl  drv_write_unsupported
            .globl  drv_ioctl_unsupported
            .globl  __sys_model

            .equ    MODEL_F_GRAPHICS,   0x01

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; ctc_init()
            ;; ----------------------------------------------------------------
            ;; resets the ctc and seeds the im 2 vector base, but leaves the
            ;; periodic scheduler source stopped. boot-time disk/driver waits
            ;; only need device completion interrupts; the os enables the
            ;; preemption tick later, once the first shell process is live.
            ;;
            ;; does NOT load the i register or enable cpu interrupts
            ;; (im 2 / ei): wiring the handler and lighting up interrupts is
            ;; the scheduler's job.
            ;;
            ;; destroys:
            ;;  a
            ;; ----------------------------------------------------------------
ctc_init::
            ;; software-reset all four channels (drops any leftover rom timer
            ;; state; unused channels stay stopped and silent).
            ld      a,#(CTC_CW | CTC_RESET)             ; 0x03
            out     (CTC_CH0),a
            out     (CTC_CH1),a
            out     (CTC_CH2),a
            out     (CTC_CH3),a

            ;; interrupt vector base via channel 0 (bit0 = 0 -> vector write).
            ld      a,#CTC_VEC_BASE
            out     (CTC_CH0),a

            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; ctc_enable_tick()
            ;; ----------------------------------------------------------------
            ;; arms the model-appropriate periodic scheduler source after the
            ;; rest of the os bootstrap is ready to tolerate preemption:
            ;;   - plain crt partner: ch2 internal timer
            ;;   - graphics partner : ch3 vertical blank counter
            ;; ----------------------------------------------------------------
ctc_enable_tick::
            ld      a,(__sys_model)
            and     #MODEL_F_GRAPHICS
            jr      nz,ctci_graphics$

            ;; plain CRT Partner: no AVDC vertical blank exists, so use channel 2
            ;; as a periodic internal timer to drive the scheduler/soft-timer tick.
            ld      a,#(CTC_INT | CTC_PRE256 | CTC_TC | CTC_RESET | CTC_CW) ; 0xa7
            out     (CTC_CH2),a
            ld      a,#CTC_TIMER_DIV
            out     (CTC_CH2),a
            ld      hl,#DRV_OK
            ret

ctci_graphics$:
            ;; channel 3 = vbl: counter mode, rising edge, interrupt enabled,
            ;; time constant follows. (edge matches the emulator's rising-edge
            ;; vb detection; verify against the schematic if it never ticks.)
            ld      a,#(CTC_INT | CTC_COUNTER | CTC_RISING | CTC_TC | CTC_RESET | CTC_CW) ; 0xd7
            out     (CTC_CH3),a
            ld      a,#CTC_VBL_DIV
            out     (CTC_CH3),a
            ld      hl,#DRV_OK
            ret

            ;; ----------------------------------------------------------------
            ;; driver vtable. dev.s enumerates the single static ctc device,
            ;; while ctc_init programs the chip once at kernel start.
            ;; open/close/read/write/ioctl are unimplemented (DRV_ERR).
            ;; ----------------------------------------------------------------
ctc_dev_drv::
            .dw     sio_dev_drv             ; next built-in driver
            .dw     0x0000                  ; probe handled in dev.s
            .dw     ctc_init                ; init (ctc chip programming)
            .dw     drv_ioctl_unsupported   ; open   (undefined for now)
            .dw     drv_ioctl_unsupported   ; close  (undefined for now)
            .dw     drv_read_unsupported    ; read   (undefined for now)
            .dw     drv_write_unsupported   ; write  (undefined for now)
            .dw     drv_ioctl_unsupported   ; ioctl  (undefined for now)

ctc_dev::
ctc_dev$:
            .dw     0x0000                  ; next
            .db     'c','t','c',0,0,0       ; name[6]
            .db     0x00                    ; flags
            .ds     DEV_DATA_SIZE           ; data[10]
            .dw     ctc_dev_drv             ; driver back pointer

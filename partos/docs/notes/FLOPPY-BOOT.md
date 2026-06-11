# Note: Minimal Floppy Boot (i8272 + Z80 DMA)

Category: Boot / Drivers / Floppy
Date(s): 2026-03-12, 2026-06-11 (merged from `2026-03-12_floppy-boot.md`)

Minimal working floppy boot sequence, extracted from original ROM behavior:
motor on, FDC sense + recalibrate, DMA programmed for memory write at
0xE000, READ DATA with N=1 (256-byte sectors — Partner standard), then
`EI`/`HALT` waiting for the FDC interrupt exactly like the ROM does, and a
7-byte result phase.

~~~asm
        ORG     0100h

floppy_min_boot:
        DI
        LD      SP,0FF00h

        ; === Motor on ===
        XOR     A
        OUT     (98h),A                 ; motor enable (same as ROM)

        ; === Quick FDC reset / sense ===
        LD      A,08h
        CALL    fdc_send
        CALL    fdc_result
        CALL    fdc_result

        ; === RECALIBRATE to track 0 ===
        CALL    fdc_recal

        ; === Z80 DMA setup: read to E000h ===
        LD      A,05h                   ; DMA reset
        OUT     (0C0h),A
        LD      A,0CFh                  ; continuous mode
        OUT     (0C0h),A
        LD      HL,0E000h
        LD      A,L
        OUT     (0C0h),A
        LD      A,H
        OUT     (0C0h),A

        ; === READ DATA command (MFM) ===
        LD      A,46h                   ; READ DATA + MFM
        CALL    fdc_send
        XOR     A                       ; drive/head = 0
        CALL    fdc_send
        XOR     A                       ; cylinder 0
        CALL    fdc_send
        XOR     A                       ; head 0
        CALL    fdc_send
        LD      A,1                     ; sector 1
        CALL    fdc_send
        LD      A,1                     ; N=1 (256 bytes per sector — Partner standard)
        CALL    fdc_send
        LD      A,18h                   ; EOT
        CALL    fdc_send
        LD      A,0Ah                   ; GPL
        CALL    fdc_send
        LD      A,0FFh                  ; DTL
        CALL    fdc_send

        EI
        HALT                            ; wait for FDC IRQ (exactly like ROM)

        ; === Read result phase (7 bytes) ===
        CALL    fdc_result
        CALL    fdc_result
        CALL    fdc_result
        CALL    fdc_result
        CALL    fdc_result
        CALL    fdc_result
        CALL    fdc_result

        HALT                            ; done — boot code is now at E000h

; Helpers (extracted from ROM)
fdc_send:
        PUSH    AF
wait_rqm:  IN      A,(0F0h)
        AND     0C0h
        CP      80h
        JR      NZ,wait_rqm
        POP     AF
        OUT     (0F1h),A
        RET

fdc_result:
wait_res:  IN      A,(0F0h)
        AND     0C0h
        CP      0C0h
        JR      NZ,wait_res
        IN      A,(0F1h)
        RET

fdc_recal:
        LD      A,0Fh
        CALL    fdc_send
        XOR     A                       ; drive 0
        CALL    fdc_send
        RET

        END
~~~

## 2026-06-11 context

- The `EI`/`HALT` wait requires the FDC interrupt vector latch at port
  0xE8 to be programmed and an IM2 vector table entry in place (see
  [IO-MAP.md](IO-MAP.md)); the original ROM sets this up in fdc_init.
- Note `OUT (98h)` with A=0 — any write to 0x98 turns the motor on
  (value irrelevant); the CTC ch0->ch1 cascade turns it off again after a
  timeout, so long operations must keep the motor alive.
- Starting point for the PartOS ROM floppy driver; will be reshaped by the
  driver format spec.

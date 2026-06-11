# Note: Minimal Hard Drive Boot (SASI / Xebec S1410)

Category: Boot / Drivers / Hard Disk
Date(s): 2026-03-12, 2026-06-11 (merged from `2026-03-12_harddrive-boot.md`)

Minimal realistic SASI boot sequence: controller reset, post-reset delay,
SEL + BSY wait, optional REQUEST SENSE wake-up, then READ(0) of one block
to 0xE000 with the Z80 DMA moving bytes from the SASI data port.

~~~asm
        ORG     0100h

hdd_min_boot_realistic:
        DI
        LD      SP,0FF00h

        ; 1. Reset SASI controller
        XOR     A
        OUT     (12h),A                 ; SASI reset

        ; 2. Short delay after reset (real hardware needs ~10–50 ms)
        LD      BC,0FFFFh
delay:  DEC     BC
        LD      A,B
        OR      C
        JR      NZ,delay

        ; 3. Select target + wait for BSY
        LD      A,01h                   ; assert SEL
        OUT     (10h),A
wait_bsy:
        IN      A,(10h)
        BIT     3,A                     ; BSY bit 3
        JR      Z,wait_bsy
        LD      A,02h                   ; release SEL, assert ATN (some controllers need it)
        OUT     (10h),A

        ; 4. Send a minimal "wake-up" / sense command (optional but recommended)
        ;    Many Xebec like to see REQUEST SENSE first after reset
        LD      HL,sense_cmd
        CALL    sasi_send_cmd

        ; 5. Wait until REQ goes away (command complete)
        CALL    sasi_wait_phase_end

        ; 6. Now do the real READ(0) of 1 block (512 bytes) to E000h
        LD      A,0C3h                  ; DMA reset
        OUT     (0C0h),A
        LD      A,79h                   ; WR0: port B→memory, byte mode, inc
        OUT     (0C0h),A
        LD      HL,0E000h
        LD      A,L                     ; dest low
        OUT     (0C0h),A
        LD      A,H                     ; dest high
        OUT     (0C0h),A
        LD      A,0FFh                  ; length low (256 bytes — adjust if 512)
        OUT     (0C0h),A
        XOR     A                       ; length high
        OUT     (0C0h),A

        LD      HL,read_cmd
        CALL    sasi_send_cmd

        ; Wait for transfer complete (simplified polling)
        CALL    sasi_wait_done

        HALT                            ; sector is at E000h — inspect or JP E000h

; ────────────────────────────────────────────────────────────────
; Helpers (very close to ROM style)
; ────────────────────────────────────────────────────────────────

sasi_send_cmd:
        PUSH    BC
        LD      B,6                     ; 6-byte command
send_loop:
        CALL    sasi_wait_req
        LD      A,(HL)
        OUT     (11h),A
        INC     HL
        DJNZ    send_loop
        POP     BC
        RET

sasi_wait_req:
wait_req:
        IN      A,(10h)
        BIT     7,A                     ; REQ bit 7
        JR      Z,wait_req
        RET

sasi_wait_phase_end:
wait_end:
        IN      A,(10h)
        BIT     7,A                     ; still REQ?
        JR      NZ,wait_end
        RET

sasi_wait_done:
        ; In real code you would poll until BSY drops or MSG phase
        ; For minimal: just a long delay or assume success
        LD      BC,0FFFFh
wait_done_loop:
        DEC     BC
        LD      A,B
        OR      C
        JR      NZ,wait_done_loop
        RET

; ────────────────────────────────────────────────────────────────
; Command blocks
; ────────────────────────────────────────────────────────────────

sense_cmd:
        DB      03h, 00h, 00h, 00h, 12h, 00h   ; REQUEST SENSE, allocation 18 bytes

read_cmd:
        DB      08h, 00h, 00h, 00h, 01h, 00h   ; READ(0), LBA=0, 1 block (512 bytes)

        END
~~~

## 2026-06-11 context

- Caution: the BSY wait at step 3 spins forever when no HDD controller is
  on the bus — and an absent controller reads 0xFF (floating bus), which
  *passes* the BSY test and then feeds garbage. A production driver needs
  a timeout plus sanity checks (e.g. 0xFF status is "nobody home"), unlike
  this minimal sequence. (The original ROM's behavior on a floating bus is
  to run into a load-validation failure and report HDD malfunction.)
- Partner HDD images use 256-byte blocks (1224x32x256); the "512 bytes"
  comment and DMA length need to match the real block size.
- Status/control bit reference is in [IO-MAP.md](IO-MAP.md); the emulator's
  adapter behavior is modeled in `lib/chipsex/xebec/idpartner_sasi.h`.

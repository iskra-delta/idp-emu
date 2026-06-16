            ;; nvram.s
            ;;
            ;; minimal rom-side NVRAM helpers for the Partner MM58167-backed
            ;; 8-byte setup block.
            ;;
            ;; native calling convention only:
            ;;
            ;;   nvram_read():
            ;;     in : hl = destination buffer (8 bytes)
            ;;     out: hl = destination advanced past the block
            ;;
            ;;   nvram_write():
            ;;     in : hl = source buffer (8 bytes)
            ;;     out: hl = source advanced past the block
            ;;
            ;;   nvram_checksum_ok():
            ;;     in : hl = pointer to 8-byte setup block
            ;;     out: a = 0x01 if valid, 0x00 if invalid
            ;;          nz on valid, z on invalid
            ;;
            ;; checksum rule:
            ;;   the sum of all 16 nibbles of the 8-byte block must be 0 mod 16
            ;;
            ;; 2026-06-14   tstih
            .module nvram

            .include "../drivers/nvram.inc"

            .globl  bios_nvram_read
            .globl  bios_nvram_write
            .globl  bios_nvram_sum
            .globl  bios_nvram_cache

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; bios_nvram_read(<hl> *dst)
            ;; ----------------------------------------------------------------
bios_nvram_read::
            ;; the mm58167 exposes the 8-byte setup block as 8 distinct ports
            ld      c,#NVRAM_PORT_BASE
            ld      b,#NVRAM_SIZE
nvram_read_loop$:
            in      a,(c)
            ld      (hl),a
            inc     hl
            inc     c
            djnz    nvram_read_loop$
            ret

            ;; ----------------------------------------------------------------
            ;; bios_nvram_write(<hl> *src)
            ;; ----------------------------------------------------------------
            ;; writes the 8-byte setup block back through the stepped mm58167
            ;; port range. each port write persists to the backing nvram file.
            ;; ----------------------------------------------------------------
bios_nvram_write::
            ld      c,#NVRAM_PORT_BASE
            ld      b,#NVRAM_SIZE
nvram_write_loop$:
            ld      a,(hl)
            out     (c),a
            inc     hl
            inc     c
            djnz    nvram_write_loop$
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= bios_nvram_sum(<hl> *block)
            ;; ----------------------------------------------------------------
            ;; returns a = sum of all 16 nibbles of the 8-byte block, mod 16.
            ;; shared by the checksum validator and the setup-save adjuster so
            ;; both agree on the rule. advances hl past the block; clobbers b,c,d.
            ;; ----------------------------------------------------------------
bios_nvram_sum::
            ld      b,#NVRAM_SIZE
            xor     a
            ld      c,a                 ; c = nibble sum mod 16

nvram_sum_loop$:
            ;; fold both nibbles of each byte into a 4-bit running sum
            ld      a,(hl)
            inc     hl
            ld      d,a                 ; d = current byte

            and     #0x0f
            add     a,c
            and     #0x0f
            ld      c,a

            ld      a,d
            rrca
            rrca
            rrca
            rrca
            and     #0x0f
            add     a,c
            and     #0x0f
            ld      c,a

            djnz    nvram_sum_loop$
            ld      a,c
            ret

            .area   _SYSVARS

            ;; rom-side cached copy of the 8-byte rtc-backed setup block
            ;; populated once during boot_main so later code can read memory
bios_nvram_cache::
            .ds     NVRAM_SIZE

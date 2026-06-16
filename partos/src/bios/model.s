            ;; model.s
            ;;
            ;; minimal rom-side machine-model detection.
            ;;
            ;; tries to distinguish between the plain text Partner and the
            ;; graphics-capable GDP model by touching only GDP-board-local
            ;; ports. on non-gdp machines these ports float to 0xff.
            ;;
            ;; native calling convention only:
            ;;   model_detect():
            ;;     out: a = 0x01 if graphics model, 0x00 otherwise
            ;;          z  set for plain model, nz for graphics model
            ;;
            ;; 2026-06-14   tstih
            .module model

            .include "../drivers/gdp.inc"

            .globl  model_detect
            .globl  model

            .equ    MODEL_F_GRAPHICS,    0x01

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; <a> <= model_detect()
            ;; ----------------------------------------------------------------
            ;; returns the low-bit model flag expected by the later kernel
            ;; page-0 metadata: bit 0 set means graphics-capable Partner.
            ;; ----------------------------------------------------------------
model_detect::
            ;; any non-0xff response from the gdp-local ports means graphics
            in      a,(GDP_PORT_PIO_COMMON)
            cp      #0xff
            jr      nz,model_graphics$

            in      a,(GDP_PORT_AVDC_STATUS)
            cp      #0xff
            jr      nz,model_graphics$

            xor     a
            ret

model_graphics$:
            ld      a,#MODEL_F_GRAPHICS
            ret

            .area   _SYSVARS

            ;; rom-side writable machine model cache.
            ;; boot_main stores the value returned in a by model_detect here.
model::
            .ds     1

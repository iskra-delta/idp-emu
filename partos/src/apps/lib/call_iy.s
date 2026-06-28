            ;; call_iy.s
            ;;
            ;; freestanding SDCC runtime helper for indirect calls via `iy`.
            ;; newer compiler output uses this form for some function-pointer
            ;; calls emitted from the C runtime/helpers.
            ;;
            ;; 2026-06-27   tstih
            .module call_iy
            .optsdcc -mz80 sdcccall(1)

            .globl  ___sdcc_call_iy
            .globl  __sdcc_call_iy

            .area   _CODE

___sdcc_call_iy:
__sdcc_call_iy:
            jp      (iy)

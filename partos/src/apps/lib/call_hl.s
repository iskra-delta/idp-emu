            ;; call_hl.s
            ;;
            ;; tiny freestanding SDCC runtime helper used for indirect C
            ;; function-pointer calls. once the apps call into the PartOS
            ;; service table through `partos_t *`, the compiler lowers those
            ;; calls to `___sdcc_call_hl`.
            ;;
            ;; 2026-06-27   tstih
            .module call_hl
            .optsdcc -mz80 sdcccall(1)

            .globl  ___sdcc_call_hl
            .globl  __sdcc_call_hl

            .area   _CODE

___sdcc_call_hl:
__sdcc_call_hl:
            push    hl
            ret

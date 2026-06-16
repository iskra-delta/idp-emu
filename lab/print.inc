            ;; print.inc
            ;;
            ;; minimal BIOS text-output helper attributes
            ;;
            ;; print_at uses one of these symbolic attributes and maps them to
            ;; the active backend (AVDC on GDP systems, VT52-style serial text
            ;; on CRT systems).
            ;;
            ;; 2026-06-13   tstih

            .equ    PRINT_ATTR_NORMAL,       0
            .equ    PRINT_ATTR_HIGHLIGHT,    1
            .equ    PRINT_ATTR_INVERSE,      2

            ;; print.inc
            ;;
            ;; minimal rom-side print_at attributes
            ;;
            ;; print_at uses:
            ;;   a  = attribute selector
            ;;   b  = x
            ;;   c  = y
            ;;   hl = pointer to zero-terminated text
            ;;
            ;; 2026-06-14   tstih

            .equ    PRINT_ATTR_NORMAL,       0
            .equ    PRINT_ATTR_HIGHLIGHT,    1
            .equ    PRINT_ATTR_INVERSE,      2

            ;; gdp.inc
            ;;
            ;; Compatibility wrapper for older GDP-named sources.
            ;;
            ;; The runtime text driver is now named AVDC, but the old include
            ;; path is kept so ROM/test sources can migrate gradually.
            ;;
            ;; 2026-06-17   tstih

            ;; keep the include path rooted from the current source directory
            ;; so both `src/drivers/*.s` and `src/rom/*.s` can include this
            ;; compatibility wrapper without depending on assembler include-dir
            ;; search behavior.
            .include "../drivers/avdc.inc"

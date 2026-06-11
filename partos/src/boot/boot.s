            ;; boot.s
            ;;
            ;; partos boot: loads the boot sector from the boot device. it
            ;; runs from the rom in place (only the bios is copied to
            ;; memory top). currently an empty placeholder that parks the
            ;; cpu.
            ;;
            ;; 2026-06-11   tstih
            .module boot

            .area   _BOOT

            ;; ----------------------------------------------------------------
            ;; boot_main()
            ;; ----------------------------------------------------------------
            ;; entered from page 0 rom init with the bios in place at memory
            ;; top. interrupts are disabled.
            ;; ----------------------------------------------------------------
boot_main::
bm_park$:
            halt
            jr      bm_park$

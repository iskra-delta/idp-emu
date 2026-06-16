            ;; main.s
            ;;
            ;; tiny ROM harness for partos boot banner testing
            ;;
            ;; 2026-06-13   tstih
            .module partos_boot_banner

            .globl  boot_main

            .area   _CODE

start::
            jp      boot_main

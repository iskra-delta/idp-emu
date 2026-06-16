            ;; stubs.s
            ;;
            ;; unresolved probe stubs for the tiny boot banner harness
            ;;
            ;; 2026-06-13   tstih
            .module partos_boot_banner_stubs

            .globl  fd_probe
            .globl  hd_probe

            .area   _CODE

fd_probe::
hd_probe::
            ld      hl,#0x0000
            ret

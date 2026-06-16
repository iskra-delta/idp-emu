            ;; stubs.s
            ;;
            ;; unresolved helper stubs for the tiny print harness
            ;;
            ;; 2026-06-13   tstih
            .module print_stubs

            .globl  append_list
            .globl  sio_probe
            .globl  rtc_probe
            .globl  nvram_probe
            .globl  fd_probe
            .globl  hd_probe

            .area   _CODE

append_list::
            ret

sio_probe::
rtc_probe::
nvram_probe::
fd_probe::
hd_probe::
            ld      hl,#0x0000
            ret

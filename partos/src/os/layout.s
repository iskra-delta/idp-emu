            ;; layout.s
            ;;
            ;; area-order anchor for os.sys.
            ;;
            ;; this module links first in the OS object list and declares the
            ;; loadable initialized-data area before the zero-fill sysvars area.
            ;; that keeps _INITIALIZED contiguous after _CODE in the linked image,
            ;; while _SYSVARS is based separately into the reserved bank-local
            ;; scratch window.
            ;;
            ;; 2026-06-22   tstih
            .module layout

            .globl  sio_console_rx_ring

            .area   _INITIALIZED
            .area   _SYSVARS

            ;; dev0 (console keyboard) RX ring backing store, placed FIRST in the
            ;; bank-local _SYSVARS window (0x1000) so it sits far below the driver
            ;; ISR stack at 0x1400. sio_init points sio_state0's RXBUF here so the
            ;; SIO RX ISR always has a real ring to fill (a null RXBUF makes
            ;; sss_ring$ silently drop keystrokes that arrive between reads).
sio_console_rx_ring::
            .ds     32                  ; must equal SIO_DEFAULT_RX in sio.inc

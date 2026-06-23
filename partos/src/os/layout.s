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

            .area   _INITIALIZED
            .area   _SYSVARS

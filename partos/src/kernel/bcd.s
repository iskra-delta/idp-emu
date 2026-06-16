            ;; bcd.s
            ;;
            ;; common packed-bcd helpers
            ;;
            ;; 2026-06-13   tstih
            .module bcd

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <a> <= bcd_to_bin(<a> bcd)
            ;; ----------------------------------------------------------------
            ;; converts one packed-bcd byte to binary.
            ;;
            ;; destroys:
            ;;  b, c
            ;; ----------------------------------------------------------------
bcd_to_bin::
            ld      b,a
            and     #0x0f
            ld      c,a
            ld      a,b
            rrca
            rrca
            rrca
            rrca
            and     #0x0f
            ld      b,a
            add     a,a
            add     a,a
            add     a,b
            add     a,a
            add     a,c
            ret

            ;; ----------------------------------------------------------------
            ;; <a> <= bin_to_bcd(<a> value)
            ;; ----------------------------------------------------------------
            ;; converts one 0-99 binary value to packed-bcd.
            ;;
            ;; destroys:
            ;;  b, c
            ;; ----------------------------------------------------------------
bin_to_bcd::
            ld      b,#0
b2b_loop$:
            cp      #10
            jr      c,b2b_pack$
            sub     #10
            inc     b
            jr      b2b_loop$
b2b_pack$:
            ld      c,a
            ld      a,b
            rlca
            rlca
            rlca
            rlca
            or      c
            ret

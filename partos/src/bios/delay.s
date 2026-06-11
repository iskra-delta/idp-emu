            ;; delay.s
            ;;
            ;; delays
            ;;
            ;; 2023-09-13   tstih
            .module delay

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; delay_1ms()
            ;; ----------------------------------------------------------------
            ;; 1ms very accurate delay. it also accounts for the call to this
            ;; function. it should be called with interrupts disabled.
            ;; on the 4 Mhz CPU 1 cycle is 1/4.000.000s.
            ;; 1 millisec = 1/1000s hence we need 4.000 of them for 1/1000 sec
            ;;
            ;; the timing for this code is:
            ;; static code
            ;;   1. init call 17 t-states
            ;;   2. ret = 10 t-states
            ;;   3. ld b,... 7 t-states
            ;;   4. last djnz = 8 t-states
            ;;   5. sum is 42 t-states, 4000 - 42 = 3958 t-states
            ;; dynamic code
            ;;   1. N*loop: (N-1)*13 + N*M = 3958
            ;;   2. N*13 - 1*13 + N*M = 3958
            ;;   3. N*13 + N*M = 3958 + 13
            ;;   4. N(M+13) = 3971
            ;;   5. 3971 = 209 * 19, N=209, M=6
            ;; total 209 * 6 + 208 *13 = 3958 + 42 init = 4000
            ;;
            ;; destroys:
            ;;  b   ... 0
            ;;  hl  ... hl-209
            ;;  flags
            ;; ----------------------------------------------------------------
delay_1ms::
            ld      b,#209              ; 7
d1m_loop$:
            dec     hl                  ; 6
            djnz    d1m_loop$           ; 13/8
            ret                         ; 10

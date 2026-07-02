            ;; cwdcall.s
            ;;
            ;; conservative app-side helper for storing the shell's current
            ;; working directory through the PartOS service table. `cd` is the
            ;; first caller that mutates this path, so keep the whole fetch +
            ;; copy sequence in assembly instead of relying on C codegen around
            ;; mixed pointer-return indirect calls.
            ;;
            ;; 2026-06-30   tstih
            .module app_cwdcall
            .optsdcc -mz80 sdcccall(1)

            .globl  _app_current_dir
            .globl  _app_store_current_dir

            .area   _CODE

_app_store_current_dir::
            ld      a,h
            or      l
            ret     z

            push    hl
            call    _app_current_dir
            pop     hl
            ld      a,d
            or      e
            ret     z

app_store_copy$:
            ld      a,(hl)
            ld      (de),a
            inc     hl
            inc     de
            or      a
            jr      nz,app_store_copy$
            ret

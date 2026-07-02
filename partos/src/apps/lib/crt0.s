            ;; crt0.s
            ;;
            ;; minimal C app entry shim for PartOS .COM tools.
            ;;
            ;; the process loader already gives each application a private
            ;; stack. this stub only bridges the COM/XL entry point to the C
            ;; bootstrap routine that binds the "partos" service, tokenizes the
            ;; command line into argc/argv and then calls main().
            ;;
            ;; toolchain note (xcc / x compiler suite):
            ;; xcc places every C global -- initialized OR zero -- into the
            ;; `_DATA` area with its literal bytes, and the process loader loads
            ;; that area into RAM as part of the image. So, unlike the old sdcc
            ;; model (separate `_INITIALIZED`/`_INITIALIZER` areas copied by a
            ;; startup pass), there is nothing to copy or zero here: the loaded
            ;; image already holds correct initial data. This stub therefore
            ;; just enters the shared C bootstrap.
            ;;
            ;; 2026-07-01   tstih
            .module app_crt0

            .globl  _app_crt0_entry
            .globl  _app_bootstrap

            .area   _CODE

_app_crt0_entry::
            call    _app_bootstrap

app_crt0_halt$:
            halt
            jr      app_crt0_halt$

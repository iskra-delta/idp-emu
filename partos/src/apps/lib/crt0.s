            ;; crt0.s
            ;;
            ;; minimal C app entry shim for PartOS .COM tools.
            ;;
            ;; the process loader already gives each application a private
            ;; stack. this stub only bridges the COM/XL entry point to the C
            ;; bootstrap routine that binds the "partos" service, tokenizes the
            ;; command line into argc/argv and then calls main().
            ;;
            ;; 2026-06-27   tstih
            .module app_crt0

            .globl  _app_crt0_entry
            .globl  _app_bootstrap
            .globl  s__DATA
            .globl  s__INITIALIZED
            .globl  s__GSFINAL

            .area   _CODE

_app_crt0_entry::
            ;; SDCC leaves C globals in the linked _DATA area; without the
            ;; usual CRT startup pass that memory simply inherits whatever the
            ;; previous process left behind. Clear it here, then copy any
            ;; explicit initializers before we enter the shared C bootstrap.
            ;;
            ;; `l__DATA` / `l__INITIALIZER` are emitted as relocatable words in
            ;; the XL image, so the process loader would add the runtime load
            ;; base to those byte counts and turn these `ldir` spans into huge
            ;; memory wipes. Derive the zero-fill length at runtime from the
            ;; relocated section bounds instead. xld places `_INITIALIZED`
            ;; after `_DATA`, while the flat sdld debug payload keeps its
            ;; `_INITIALIZED` block before `_DATA`, so pick the first bound
            ;; that actually lies above `_DATA`.
            ;;
            ;; Current PartOS C tools only use zero-initialized runtime data;
            ;; when non-zero RAM initializers are needed we should add a
            ;; dedicated non-relocating byte-count marker for that copy too.
            ld      hl,#s__INITIALIZED
            ld      de,#s__DATA
            or      a
            sbc     hl,de
            jr      nc,app_crt0_have_len$
            ld      hl,#s__GSFINAL
            ld      de,#s__DATA
            or      a
            sbc     hl,de
app_crt0_have_len$:
            ld      b,h
            ld      c,l
            ld      a,b
            or      c
            jr      z,app_crt0_boot$
            ld      hl,#s__DATA
            xor     a
            ld      (hl),a
            dec     bc
            ld      a,b
            or      c
            jr      z,app_crt0_boot$
            ld      d,h
            ld      e,l
            inc     de
            ldir

app_crt0_boot$:
            call    _app_bootstrap

app_crt0_halt$:
            halt
            jr      app_crt0_halt$

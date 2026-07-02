            ;; fscall.s
            ;;
            ;; tiny PartOS app-side bridge for FAT-style service-table calls.
            ;;
            ;; these filesystem entries are hand-written assembly on the OS
            ;; side and use a stack layout that is easiest to preserve from a
            ;; matching assembly caller. The higher-level apps still obtain the
            ;; public `partos_t` table and route through it; this file only
            ;; performs the final indirect call with the historical argument
            ;; order.
            ;;
            ;; 2026-06-29   tstih
            .module app_fscall
            .optsdcc -mz80 sdcccall(1)

            .equ    PARTOS_OFF_MOUNT_BY_NAME, 74
            .equ    PARTOS_OFF_LOOKUP_PATH,   76
            .equ    PARTOS_OFF_OPEN_FILE,     78
            .equ    PARTOS_OFF_CREATE_FILE,   80
            .equ    PARTOS_OFF_READ_FILE,     82
            .equ    PARTOS_OFF_WRITE_FILE,    84
            .equ    PARTOS_OFF_READDIR,       86
            .equ    PARTOS_OFF_UNLINK_PATH,   94
            .equ    PARTOS_OFF_MKDIR_PATH,    96
            .equ    PARTOS_OFF_RMDIR_PATH,    98
            .equ    FAT_EINVAL,               0xfffe

            .globl  _app_partos
            .globl  _app_open_event

            .globl  _app_mount_fs
            .globl  _app_lookup_path
            .globl  _app_open_file
            .globl  _app_create_file
            .globl  _app_read_file
            .globl  _app_write_file
            .globl  _app_read_directory
            .globl  _app_unlink_path
            .globl  _app_mkdir_path
            .globl  _app_rmdir_path

            .area   _CODE

_app_mount_fs::
            ld      a,h
            or      l
            jr      z,app_mount_einval$
            ld      a,d
            or      e
            jr      z,app_mount_einval$

            push    hl                  ; save fs
            push    de                  ; save dev_name
            call    _app_partos         ; de = partos table
            ld      a,d
            or      e
            jr      z,app_mount_fail_pop$
            ex      de,hl
            ld      bc,#PARTOS_OFF_MOUNT_BY_NAME
            add     hl,bc
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      a,b
            or      c
            jr      z,app_mount_fail_pop$
            ld      (app_call_fn$),bc
            call    _app_open_event     ; de = event
            ld      a,d
            or      e
            jr      z,app_mount_fail_pop$
            ld      (app_call_evt$),de
            pop     de                  ; restore dev_name
            pop     hl                  ; restore fs
            ld      (app_call_arg0$),hl
            ld      (app_call_arg1$),de

            ld      de,(app_call_evt$)
            push    de                  ; target sees [ret][event]
            ld      hl,(app_call_arg0$)
            ld      de,(app_call_arg1$)
            push    ix
            pop     bc
            ld      (app_saved_ix$),bc
            ld      bc,(app_call_fn$)
            call    app_call_bc$
            ld      hl,(app_saved_ix$)
            push    hl
            pop     ix
            ret

app_mount_fail_pop$:
            pop     bc                  ; discard saved dev_name
            pop     bc                  ; discard saved fs
app_mount_einval$:
            ld      de,#FAT_EINVAL
            ret

_app_lookup_path::
            ld      bc,#PARTOS_OFF_LOOKUP_PATH
            jp      app_path_common$

_app_open_file::
            ld      bc,#PARTOS_OFF_OPEN_FILE
            jp      app_path_common$

_app_create_file::
            ld      bc,#PARTOS_OFF_CREATE_FILE
            jp      app_path_common$

_app_read_directory::
            ld      bc,#PARTOS_OFF_READDIR
            jp      app_path_common$

_app_unlink_path::
            ld      bc,#PARTOS_OFF_UNLINK_PATH
            jp      app_path_common$

_app_mkdir_path::
            ld      bc,#PARTOS_OFF_MKDIR_PATH
            jp      app_path_common$

_app_rmdir_path::
            ld      bc,#PARTOS_OFF_RMDIR_PATH
            jp      app_path_common$

            ;; int16_t app_*_path(fs, path, result/info)
            ;;   in: hl=fs, de=path, stack=[ret][result/info]
app_path_common$:
            ld      a,h
            or      l
            jr      z,app_ret_einval2$
            ld      a,d
            or      e
            jr      z,app_ret_einval2$

            push    hl                  ; save fs
            push    de                  ; save path
            ld      hl,#6
            add     hl,sp
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,app_path_fail_pop$

            ld      (app_call_off$),bc
            call    _app_partos         ; de = partos table
            ld      a,d
            or      e
            jr      z,app_path_fail_pop$
            ex      de,hl
            ld      bc,(app_call_off$)
            add     hl,bc
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      a,b
            or      c
            jr      z,app_path_fail_pop$
            ld      (app_call_fn$),bc
            call    _app_open_event     ; de = event
            ld      a,d
            or      e
            jr      z,app_path_fail_pop$
            ld      (app_call_evt$),de
            pop     de                  ; restore path
            pop     hl                  ; restore fs
            ld      (app_call_arg0$),hl
            ld      (app_call_arg1$),de

            ld      hl,#2
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = result/info
            push    bc
            ld      de,(app_call_evt$)
            push    de                  ; target sees [ret][event][result]

            ld      hl,(app_call_arg0$)
            ld      de,(app_call_arg1$)
            push    ix
            pop     bc
            ld      (app_saved_ix$),bc
            ld      bc,(app_call_fn$)
            call    app_call_bc$
            ld      hl,(app_saved_ix$)
            push    hl
            pop     ix
            jp      app_ret_clean2$

app_path_fail_pop$:
            pop     bc                  ; discard saved path
            pop     bc                  ; discard saved fs
app_ret_einval2$:
            ld      de,#FAT_EINVAL
            jp      app_ret_clean2$

_app_read_file::
            ld      bc,#PARTOS_OFF_READ_FILE
            jp      app_file_common$

_app_write_file::
            ld      bc,#PARTOS_OFF_WRITE_FILE
            jp      app_file_common$

            ;; int16_t app_*_file(file, buf, bytes)
            ;;   in: hl=file, de=buf, stack=[ret][bytes]
app_file_common$:
            ld      a,h
            or      l
            jr      z,app_ret_einval2$
            ld      a,d
            or      e
            jr      z,app_ret_einval2$

            push    hl                  ; save file
            push    de                  ; save buf
            ld      hl,#6
            add     hl,sp
            ld      a,(hl)
            inc     hl
            or      (hl)
            jr      z,app_file_fail_pop$

            ld      (app_call_off$),bc
            call    _app_partos         ; de = partos table
            ld      a,d
            or      e
            jr      z,app_file_fail_pop$
            ex      de,hl
            ld      bc,(app_call_off$)
            add     hl,bc
            ld      c,(hl)
            inc     hl
            ld      b,(hl)
            ld      a,b
            or      c
            jr      z,app_file_fail_pop$
            ld      (app_call_fn$),bc
            call    _app_open_event
            ld      a,d
            or      e
            jr      z,app_file_fail_pop$
            ld      (app_call_evt$),de
            pop     de                  ; restore buf
            pop     hl                  ; restore file
            ld      (app_call_arg0$),hl
            ld      (app_call_arg1$),de

            ld      hl,#2
            add     hl,sp
            ld      c,(hl)
            inc     hl
            ld      b,(hl)              ; bc = bytes
            ld      (app_call_arg2$),bc
            ld      de,(app_call_evt$)
            push    de                  ; event first
            ld      bc,(app_call_arg2$)
            push    bc                  ; target sees [ret][bytes][event]

            ld      hl,(app_call_arg0$)
            ld      de,(app_call_arg1$)
            push    ix
            pop     bc
            ld      (app_saved_ix$),bc
            ld      bc,(app_call_fn$)
            call    app_call_bc$
            ld      hl,(app_saved_ix$)
            push    hl
            pop     ix
            jp      app_ret_clean2$

app_file_fail_pop$:
            pop     bc                  ; discard saved buf
            pop     bc                  ; discard saved file
            ld      de,#FAT_EINVAL
            jp      app_ret_clean2$

app_call_bc$:
            push    bc
            ret

app_ret_clean2$:
            pop     hl                  ; caller return address
            pop     bc                  ; drop original stacked arg
            jp      (hl)

            .area   _DATA

app_call_off$:
            .ds     2
app_call_fn$:
            .ds     2
app_call_evt$:
            .ds     2
app_call_arg0$:
            .ds     2
app_call_arg1$:
            .ds     2
app_call_arg2$:
            .ds     2
app_saved_ix$:
            .ds     2

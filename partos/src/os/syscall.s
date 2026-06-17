            ;; syscall.s
            ;;
            ;; os syscall service registration
            ;;
            ;; this module publishes the empty "yos" service table through the
            ;; existing named-service registry. RST 0x10 already resolves one
            ;; service name to its function-table pointer, so registering the
            ;; OS service here gives future syscall work one stable anchor.
            ;;
            ;; for now the table contains only a zero terminator.
            ;;
            ;; 2026-06-17   tstih
            .module syscall

            .globl  _syscall_init
            .globl  _syscall_service
            .globl  _syscall_table
            .globl  _svc_register

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; <de> <= _syscall_init()
            ;; ----------------------------------------------------------------
            ;; registers the OS "yos" service once and returns the service
            ;; object. later calls simply return the cached pointer.
            ;; ----------------------------------------------------------------
_syscall_init::
            ld      de,(_syscall_service)
            ld      a,d
            or      e
            ret     nz
            ld      hl,#syscall_name$
            ld      de,#_syscall_table
            call    _svc_register
            ld      (_syscall_service),de
            ret

            .area   _INITIALIZED

_syscall_service::
            .dw     0x0000

_syscall_table::
            .dw     0x0000

syscall_name$:
            .db     'y','o','s',0

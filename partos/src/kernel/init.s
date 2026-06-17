            ;; init.s
            ;;
            ;; early kernel entry. the current rom loads an os image into
            ;; 0xe000..0xffff, jumps to __sys_page0_install at its fixed kernel
            ;; page-0 block address, and
            ;; currently passes hl=0xe000 as the continuation address. once
            ;; low page is installed into both banks, control eventually lands
            ;; in the loaded image continuation path and may reach here.
            ;;
            ;; 2026-06-14   tstih
            .module init

            .include "../partos.inc"
            .include "../drivers/ctc.inc"

            .globl  __sys_kernel
            .globl  __usr_heap
            .globl  __sys_heap
            .globl  _ir_init
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _mem_init
            .globl  _ir_set
            .globl  _ir_vbl_handler
            .globl  _vector_set
            .globl  _svc_query_rst10
            .globl  _syscall_init
            .globl  _thread_create
            .globl  _thread_resume
            .globl  __thread_robin
            .globl  _kernel_bootstrap
            .globl  _dev_init
            .globl  _drv_register_all
            .globl  _dev_init_all
            .globl  _dev_probe_all

            .equ    VECTOR_RST10,       2
            .equ    BOOT_THREAD_STACK,  64

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; __sys_kernel()
            ;; ----------------------------------------------------------------
            ;; first shared-memory kernel instruction stream after page-0
            ;; installation. this is intentionally minimal for now: it marks
            ;; the canonical kernel entry and parks safely until the real
            ;; early-init sequence is added.
            ;; ----------------------------------------------------------------
__sys_kernel::
            di
            ld      sp,#KERNEL_STACK_TOP
            ;; init interrupts
            call    _ir_init
            call    _ir_disable         ; ref count di!
            ;; init heap and memory management
            ld      hl,#__usr_heap
            ld      de,#USER_HEAP_SIZE
            call    _mem_init
            ld      hl,#__sys_heap
            ld      de,#KERNEL_HEAP_SIZE
            call    _mem_init
            ;; install the vbl (ctc ch3) timer-tick handler into the im 2 table.
            ;; interrupts stay off (no im 2 / ei here): arming the ctc and
            ;; enabling interrupts is the scheduler bring-up's job.
            ld      a,#CTC_VEC_VBL
            ld      de,#_ir_vbl_handler
            call    _ir_set
            ;; install the rst 0x10 bridge so code can resolve named services
            ;; through the page-0 syscall entry. the current bridge only does
            ;; service lookup (hl = name -> de = function table).
            ld      a,#VECTOR_RST10
            ld      de,#_svc_query_rst10
            call    _vector_set
            ;; bring up the device layer: run the driver-level inits, then
            ;; enumerate every device configured in nvram into the device chain.
            call    _dev_init
            call    _dev_init_all
            call    _dev_probe_all

            ;; register the kernel "yos" syscall service. the table is still
            ;; empty today, but registering it now makes rst 0x10 lookup live.
            call    _syscall_init

            ;; create one bootstrap kernel thread that will mount the boot
            ;; volume, load shell as a process image and then retire.
            ld      hl,#_kernel_bootstrap
            ld      de,#BOOT_THREAD_STACK
            ld      bc,#0x0000
            push    bc                  ; process = NONE
            call    _thread_create
            pop     bc
            ld      a,d
            or      e
            jr      z,__sys_kernel_sched$
            ex      de,hl
            call    _thread_resume

            ;; threading is now available, so switch the vbl interrupt from the
            ;; simple timer-only handler to the real round-robin scheduler tick.
            ld      a,#CTC_VEC_VBL
            ld      de,#__thread_robin
            call    _ir_set

__sys_kernel_sched$:
            ;; the low-page and im 2 tables are already wired; now enable the
            ;; actual scheduler/driver interrupt flow and drop into the idle
            ;; halt loop so the vbl tick can dispatch the bootstrap thread.
            im      2
            call    _ir_enable

__sys_kernel_idle$:
            halt
            jr      __sys_kernel_idle$

            ;; entry.s
            ;;
            ;; fixed OS payload entry at 0xc000.
            ;;
            ;; the ROM loads the split images, jumps to the low-page kernel at
            ;; 0x0000, and the kernel's first payload thread starts here at the
            ;; shared services base. this entry performs the minimal OS bring-up
            ;; that must exist before the higher-level bootstrap thread runs:
            ;;
            ;;   - detect the machine class and cache it in __sys_model
            ;;   - snapshot the 8-byte RTC/NVRAM setup block for the drivers
            ;;   - install the rst 0x10 service-query bridge
            ;;   - wire the CTC vertical-blank IM2 vector to the scheduler tick
            ;;   - hook the soft-timer chain into the kernel tick dispatcher
            ;;   - initialize and enumerate the built-in drivers
            ;;   - register the public "partos" syscall service
            ;;   - continue into the OS bootstrap thread
            ;;
            ;; 2026-06-22   tstih
            .module entry

            .include "../partos.inc"
            .include "../drivers/ctc.inc"
            .include "../drivers/gdp.inc"
            .include "../drivers/nvram.inc"

            .globl  __os_entry

            .globl  _kcall_init
            .globl  _ir_disable
            .globl  _ir_enable
            .globl  _ir_set
            .globl  _vector_set
            .globl  __thread_robin
            .globl  __svc_query_rst10
            .globl  __sys_vec_tick
            .globl  __tmr_chain
            .globl  __drv_register_all
            .globl  __dev_init
            .globl  __dev_init_all
            .globl  __dev_probe_all
            .globl  _console_init
            .globl  _syscall_init
            .globl  _kernel_bootstrap
            .globl  __boot_model_cache
            .globl  s__SYSVARS
            .globl  l__SYSVARS
            .globl  __sys_version
            .globl  __sys_model
            .globl  __sys_flags
            .globl  __sys_meta1
            .globl  __sys_nvram_cache

            .equ    MODEL_F_GRAPHICS,    0x01
            .equ    VECTOR_RST10,        0x10

            .area   _CODE

            ;; ----------------------------------------------------------------
            ;; __os_entry()
            ;; ----------------------------------------------------------------
            ;; first thread entry inside os.sys. runs under the scheduler with
            ;; interrupts enabled, so take one outer ir bracket across the whole
            ;; setup sequence and release it only once the bridges, vectors and
            ;; driver state are coherent.
            ;; ----------------------------------------------------------------
__os_entry::
            ;; discover the kernel ABI table (rst 0x08) and patch the OS->kernel
            ;; trampolines FIRST -- every kernel call below (starting with the
            ;; very next _ir_disable) is now routed through those trampolines.
            call    _kcall_init
            call    _ir_disable

            ;; os.sys now ships code + initialized data only; clear the shared
            ;; _SYSVARS BSS region before any of its scratch globals are used.
            ld      hl,#s__SYSVARS
            ld      bc,#l__SYSVARS
            xor     a
ose_zero_sysvars$:
            ld      (hl),a
            inc     hl
            dec     bc
            ld      a,b
            or      c
            jr      nz,ose_zero_sysvars$

            ld      a,#0x10
            ld      (__sys_version),a
            xor     a
            ld      (__sys_flags),a
            ld      (__sys_meta1),a

            ld      a,(__boot_model_cache)
            bit     7,a
            jr      z,ose_detect_model$
            and     #MODEL_F_GRAPHICS
            jr      ose_model_ready$
ose_detect_model$:
            call    os_detect_model$
            jr      ose_model_ready$
ose_model_ready$:
            ld      (__sys_model),a

            call    os_copy_nvram$

            ld      a,#VECTOR_RST10
            ld      de,#__svc_query_rst10
            call    _vector_set

            ld      a,(__sys_model)
            and     #MODEL_F_GRAPHICS
            ld      a,#CTC_VEC_TICK
            jr      z,ose_tick_vec_ready$
            ld      a,#CTC_VEC_VBL
ose_tick_vec_ready$:
            ld      de,#__thread_robin
            call    _ir_set

            ld      hl,#__tmr_chain
            ld      (__sys_vec_tick),hl

            call    __drv_register_all
            call    __dev_init
            call    __dev_init_all
            call    __dev_probe_all
            call    _console_init
            call    _syscall_init

            ;; keep the outer disable bracket held across the tail handoff so a
            ;; timer interrupt cannot preempt this very first OS thread between
            ;; setup completion and the bootstrap entry.
            jp      _kernel_bootstrap

            ;; ----------------------------------------------------------------
            ;; <a> <= os_detect_model$()
            ;; ----------------------------------------------------------------
            ;; duplicate the ROM's GDP probe locally so the OS can classify the
            ;; running machine without depending on a ROM-private sysvar layout.
            ;; ----------------------------------------------------------------
os_detect_model$:
            in      a,(GDP_PORT_PIO_COMMON)
            cp      #0xff
            jr      nz,os_graphics$

            in      a,(GDP_PORT_AVDC_STATUS)
            cp      #0xff
            jr      nz,os_graphics$

            xor     a
            ret

os_graphics$:
            ld      a,#MODEL_F_GRAPHICS
            ret

            ;; ----------------------------------------------------------------
            ;; os_copy_nvram$()
            ;; ----------------------------------------------------------------
            ;; snapshot the 8-byte MM58167-backed setup block into the OS-owned
            ;; cache the drivers read during init/probe.
            ;; ----------------------------------------------------------------
os_copy_nvram$:
            ld      hl,#__sys_nvram_cache
            ld      c,#NVRAM_PORT_BASE
            ld      b,#NVRAM_SIZE
ocn_loop$:
            in      a,(c)
            ld      (hl),a
            inc     hl
            inc     c
            djnz    ocn_loop$
            ret

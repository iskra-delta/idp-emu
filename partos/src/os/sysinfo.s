            ;; sysinfo.s
            ;;
            ;; OS-owned system-info block. this used to live in the kernel's
            ;; page-0 template, but it is OS data: the syscall service POPULATES
            ;; it (from kernel identity + the live list heads it reads through
            ;; the kernel ABI) and serves it to callers. the kernel only owns the
            ;; few identity bytes (__sys_version/model/flags/meta1, imported from
            ;; the low page); everything aggregated here -- including the device
            ;; and process lists, which the kernel knows nothing about -- belongs
            ;; to the OS image.
            ;;
            ;; __sys_nvram_cache is the 8-byte CMOS/NVRAM snapshot the drivers
            ;; read; it shares the head of the block as before.
            ;;
            ;; 2026-06-21   tstih
            .module sysinfo

            .globl  __sys_version
            .globl  __sys_model
            .globl  __sys_flags
            .globl  __sys_meta1
            .globl  __sys_info
            .globl  __sys_info_end
            .globl  __sys_nvram_cache
            .globl  __sys_info_version
            .globl  __sys_info_model
            .globl  __sys_info_flags
            .globl  __sys_info_meta1
            .globl  __sys_info_drv_first
            .globl  __sys_info_dev_first
            .globl  __sys_info_svc_first
            .globl  __sys_info_process_first
            .globl  __sys_info_evt_first
            .globl  __sys_info_tmr_first
            .globl  __sys_info_thread_current
            .globl  __sys_info_thread_running
            .globl  __sys_info_thread_waiting
            .globl  __sys_info_thread_suspended
            .globl  __sys_info_thread_terminated
            .globl  __sys_info_sys_heap
            .globl  __sys_info_usr_heap
            .globl  __sys_info_reserved

            .area   _INITIALIZED

            ;; machine identity, derived from NVRAM by the OS at boot. (used to
            ;; live in the kernel low page at 0x04-0x07, but the kernel never
            ;; populated it -- it is OS-owned.)
__sys_version::
            .db     0                   ; high nibble = major, low nibble = minor
__sys_model::
            .db     0                   ; bit 0 = has graphics (gdp)
                                        ; bits 1-3 = floppy count
                                        ; bits 4-5 = hard-drive count
                                        ; bits 6-7 = reserved
__sys_flags::
            .db     0                   ; bit 0 = current bank (0/1)
__sys_meta1::
            .db     0

__sys_info::
__sys_nvram_cache::
            .db     0,0,0,0,0,0,0,0
__sys_info_version::
            .db     0
__sys_info_model::
            .db     0
__sys_info_flags::
            .db     0
__sys_info_meta1::
            .db     0
__sys_info_drv_first::
            .dw     0
__sys_info_dev_first::
            .dw     0
__sys_info_svc_first::
            .dw     0
__sys_info_process_first::
            .dw     0
__sys_info_evt_first::
            .dw     0
__sys_info_tmr_first::
            .dw     0
__sys_info_thread_current::
            .dw     0
__sys_info_thread_running::
            .dw     0
__sys_info_thread_waiting::
            .dw     0
__sys_info_thread_suspended::
            .dw     0
__sys_info_thread_terminated::
            .dw     0
__sys_info_sys_heap::
            .dw     0
__sys_info_usr_heap::
            .dw     0
__sys_info_reserved::
__sys_info_end::

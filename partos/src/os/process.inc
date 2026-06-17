            ;; process.inc
            ;;
            ;; include definitions for the PartOS OS-layer process subsystem
            ;;
            ;; 2026-06-17   tstih

            .equ    MAX_PNAME_LEN,       8

            .equ    PROCESS_INTERNAL,    0x01

            .equ    PROCESS_LOAD_OK,             0
            .equ    PROCESS_LOAD_ERR_NOT_FOUND,  1
            .equ    PROCESS_LOAD_ERR_ALLOC,      2
            .equ    PROCESS_LOAD_ERR_READ,       3
            .equ    PROCESS_LOAD_ERR_XL_INVALID, 4
            .equ    PROCESS_LOAD_ERR_XL_START,   5

            ;; process_t layout
            .equ    PROCESS_PFLAGS,      4
            .equ    PROCESS_PNAME,       5
            .equ    PROCESS_MAIN_THREAD, 13
            .equ    PROCESS_SIZE,        15

            ;; thread_t layout bits needed by process.s
            .equ    THREAD_PROCESS,      22

            ;; XL relocatable image layout
            .equ    XL_HDR_SIZE,         12
            .equ    XL_RELOC_SIZE,       4
            .equ    XL_OFF_MAGIC0,       0
            .equ    XL_OFF_MAGIC1,       1
            .equ    XL_OFF_VERSION,      2
            .equ    XL_OFF_FLAGS,        3
            .equ    XL_OFF_ENTRY,        4
            .equ    XL_OFF_CODE_SIZE,    6
            .equ    XL_OFF_RELOC_CNT,    8

            ;; heap block header offsets
            .equ    SYSOBJ_OWNER,        2
            .equ    BLK_SIZE,            7

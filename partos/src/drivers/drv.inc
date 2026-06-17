            ;; drv.inc
            ;;
            ;; common bios driver helpers and shared dev.data[] layout
            ;;
            ;; 2026-06-13   tstih

            ;; --- generic driver return codes ---------------------------------
            .equ    DRV_OK,         0x0000
            .equ    DRV_ERR,        0xffff

            ;; --- async driver model ------------------------------------------
            ;; reads and writes are asynchronous: the caller creates a sync
            ;; event, passes it in, and waits on it; the driver signals the
            ;; event when the transfer completes.
            ;;
            ;;   async read/write convention:
            ;;     in : hl = *dev, de = *buf, bc = count, ix = *event (or 0)
            ;;     out: hl = DRV_OK (accepted) / DRV_ERR (could not start)
            ;;     completion: drv_signal_done(event) -> state = signaled
            ;;
            ;; drivers that cannot truly overlap i/o (polled port drivers) just
            ;; do the work inline and signal the event immediately ("fake
            ;; async"); thread_wait4events sees the already-signaled event and
            ;; returns without blocking. real dma/interrupt drivers signal from
            ;; their completion isr instead.
            ;;
            ;; event object (must match kernel/evt.s):
            .equ    EVENT_STATE,    4   ; state byte offset in event_t
            .equ    EV_NONSIGNALED, 0
            .equ    EV_SIGNALED,    1

            ;; --- shared disk-style ioctl commands ----------------------------
            ;; de points to a little-endian 24-bit byte offset buffer.
            .equ    IOCTL_GETPOS,   0x10
            .equ    IOCTL_SETPOS,   0x11

            ;; --- shared dev.data[] offsets -----------------------------------
            .equ    DRV_DATA_UNIT,  0   ; physical unit / target number
            .equ    DRV_DATA_POS0,  1   ; byte offset low
            .equ    DRV_DATA_POS1,  2   ; byte offset mid
            .equ    DRV_DATA_POS2,  3   ; byte offset high

            ;; --- shared async stream-state prefix ----------------------------
            ;; sio and pio intentionally keep the same first 35 bytes so their
            ;; owner / ring / queue helpers can live in one common module.
            .equ    DRV_ST_DEV,     0
            .equ    DRV_ST_DATA,    2
            .equ    DRV_ST_CTRL,    3
            .equ    DRV_ST_LOCK,    4
            .equ    DRV_ST_RXBUF,   6
            .equ    DRV_ST_TXBUF,   8
            .equ    DRV_ST_RXSZ,    10
            .equ    DRV_ST_TXSZ,    11
            .equ    DRV_ST_RXHEAD,  12
            .equ    DRV_ST_RXTAIL,  13
            .equ    DRV_ST_RXCOUNT, 14
            .equ    DRV_ST_TXHEAD,  15
            .equ    DRV_ST_TXTAIL,  16
            .equ    DRV_ST_TXCOUNT, 17
            .equ    DRV_ST_RDBUF,   18
            .equ    DRV_ST_RDLEFT,  20
            .equ    DRV_ST_RDEVT,   22
            .equ    DRV_ST_RDOWNER, 24
            .equ    DRV_ST_WRBUF,   26
            .equ    DRV_ST_WRLEFT,  28
            .equ    DRV_ST_WREVT,   30
            .equ    DRV_ST_WROWNER, 32
            .equ    DRV_ST_WRACT,   34

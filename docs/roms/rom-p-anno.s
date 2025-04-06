        ;; rom-p-anno.s
        ;; 
        ;; The annotated
        ;; Iskra Delta Partner model P (text only).
	;;
        ;; MIT License (see: LICENSE)
        ;; copyright (c) 2021 tomaz stih
        ;;
	;; 04.04.2021   tstih
	.module romp
       
        .globl  start
        .globl  init
        .globl  stack
        .globl  memtest
        .globl  print_hl

        ;; SIO chips 1 and 2
        .equ    SIO1_A_CTL,     0xd9
        .equ    SIO1_A_DTA,     0xd8
        .equ    SIO1_B_CTL,     0xdb
        .equ    SIO1_B_DTA,     0xdb
        .equ    SIO2_A_CTL,     0xe1
        .equ    SIO2_A_DTA,     0xe0
        .equ    SIO2_B_CTL,     0xe4
        .equ    SIO2_B_DTA,     0xe3


        ;; Xebec S1410 ports
        .equ    XS1410_STATUS,  0x10    ; read
        .equ    XS1410_CMD,     0x10    ; write
        .equ    XS1410_DATA,    0x11    ; r/w data 
        .equ    XS1410_INT      0x12    ; r/w controller interrupts
        .equ    XS1410_DEVSEL   0xc0    ; select device
        .equ    XS1410_CFG      0xc8    ; controller config
        .equ    XS1410_HDDCFG   0xc9    ; various HDD config options

        ;; memory bank ports
        .equ    BANK1,          0x88
        .equ    BANK2,          0x90

        ;; ascii control chars
        .equ    FS,             0x1c
        .equ    CR,             0x0d    ; beg. of line
        .equ    LF,             0x0a    ; next line

        ;; @0x0000
        .area	_CODE
start::
        jp      init        
        ;; ... cont ...

        ;; @0x00c1
        ;; this looks like memory test
memtest::
        ld      hl,#s_memtest   ; some data
        call    print_hl        ; print TESTING MEMORY ...

        ld      hl,#0x2000
        ld      bc,#0xff80
        ld      d,#0x02         ; counter
        ld      a,#0x00
mt_loop:
        push    de
        push    af
        call    init_membank
        pop     af
        pop     de
        add     a,0x55
        dec     d
        jr      nz,mt_loop
        ret

        ;; @0x00de
init_membank:
        call    l0022           ; default bank is 1!
        out     (BANK2),a       ; switch memory bank 2
        call    l0022
        call    l0023
        out     (BANK1),a       ; swtich back to mem bank 1
        call    l0023
        ret


        ;; L0019 write string at hl
        ;; end of string?
print_hl::
        ld      a,(hl)
        or      a
        ret     z
        ;;  print char
        call    print_a
        ;; ... cpmt ...


        ;; L0018 write char
        ;; looks like sending char to the screen
print_a:  
        push    af              ; store char
pa_loop:
        in      a,(SIO1_A_CTL)  ; get SIO control...
        bit     2,a             ; wait until ready to send data
        jr      z,loop
        pop     af              ; restore char
        out     (SIO1_A_DTA),a  ; output letter in a
        ret


        ;; L0001 @0x0137
init:: 
        ;; initialize all 4 SIO ports
        ld      c,#SIO1_A_CTL
        ld      hl,#sio_init_data
        ld      b,#7
        otir
        ld      c,#SIO1_B_CTL
        ld      hl,#sio_init_data
        ld      b,#7
        otir
        ld      c,#SIO2_A_CTL
        ld      hl,#sio_init_data
        ld      b,#7
        otir
        ld      c,#SIO2_A_CTL
        ld      hl,#sio_init_data
        ld      b,#7
        otir
        
        ;; initialize stack
        ld      sp,#stack

        ;; mem test...
        call    memtest
        ;; ... cont ...



        ;; @0x0196
s_intro:
        .db     FS, "DELTA PARTNER /F", 0x00
        ;; @0x01a8
s_memtest:
        .db     CR, LF, LF, "TESTING MEMORY ... ", 0x00 

        ;; initialization values for SIO chips (all of them)
        ;; @0x018f
sio_init_data:
        .db     0x18, 0x04, 0x44, 0x03, 0xc1, 0x05, 0x68  

        

        ;; @0x068b
xs1410_send_cmd:
        ;; send 1 to Xebec S1410 command register.
        ld      a,#01           ; 
        out     (XS1410_CMD),a  ; 

068B   LD A,#01           ; Load the A register with the value 0x01
068D   OUT (#10),A        ; Write the value in the A register to port 0x10. This likely sets a bit in the command byte to indicate the specific command or operation to be performed.
068F   IN A,(#10)         ; Read the status information from the controller by inputting a value from port 0x10 into the A register.
0691   AND #08            ; Check the 4th bit of the status byte by performing a bitwise AND operation with the value 0x08. This bit indicates whether the controller is currently busy performing an operation.
0693   JP Z,#068F         ; Jump to address 0x068F if the 4th bit of the status byte is 0 (i.e., if the controller is not busy). This creates a loop that waits for the operation to complete.
0696   LD A,#02           ; Load the A register with the value 0x02
0698   OUT (#10),A        ; Write the value in the A register to port 0x10. This likely sets a bit in the command byte to reset the controller or signal the end of an operation.
069A   RET                ; Return from the subroutine.


       ;; @0xffc0 
stack::
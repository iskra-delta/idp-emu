# Notes from 21. mar 2021

## Xebecorama (Links to HDD controller sources) 

http://ftpmirror.your.org/pub/misc/bitsavers/pdf/xebec/

https://github.com/Anamon/pcem/blob/0a8b0ac50de7a089ac23007acbad2dfdffc75228/src/mfm_xebec.c

https://github.com/WildfireDEV/android_kernel_htc_m7/blob/25d8fee0f898d41eee3e57db47c184ccbaa0647d/drivers/block/xd.h

https://github.com/eunuchs/unix-archive/blob/a80c800a6288d8e613e057fae8313d532836598f/PDP-11/Trees/2.11BSD/usr/src/sys/OTHERS/scsi2/README

https://github.com/eunuchs/unix-archive/blob/a80c800a6288d8e613e057fae8313d532836598f/PDP-11/Trees/2.11BSD/usr/src/sys/OTHERS/scsi2/xe.c

## Xebec S1410 commands

Byte 0: Command
Byte 1: Cylinder address (MSB)
Byte 2: Cylinder address (LSB)
Byte 3: Head address (bits 0-3) and sector address (bits 4-7)
Byte 4: Sector count (1-16)
Byte 5: Data transfer length (in sectors)

Logical address =
( CYADR * HDCYL + HDADR ) * SETRK + SEADR
CYADR... cylinder address
HDADR ... head address
SEADR ... sector address
HDCYL ... heads per cylinder
SETRK ... sectors per track

### Registers

        ;; Xebec S1410 ports
        .equ    XS1410_STATUS,  0x10    ; read
        .equ    XS1410_CMD,     0x10    ; write
        .equ    XS1410_DATA,    0x11    ; r/w data 
        .equ    XS1410_INT      0x12    ; r/w controller interrupts
        .equ    XS1410_DEVSEL   0xc0    ; select device
        .equ    XS1410_CFG      0xc8    ; controller config
        .equ    XS1410_HDDCFG   0xc9    ; various HDD config options


### Test drive ready command
byte 0 = 0
byte 1, bit 5 = d (drive number, 0 or 1)
bytes 2-5 = don't care, set to 0


## Partner ROM code notes

### 1. FDCInit is not floppy disk initialization only

~~~asm
;;FDCInit:
02FD	DI
02FE	IM	2
0300	LD	HL,#02E8
0303	LD	A,L
0304	OUT	(#E8),A     ; sets the RDC interrupt address?
0306	OUT	(#C8),A     ; <-- not a floppy operation but CTC ch 0
0308	LD	A,H
0309	LD	I,A         ; <-- set int. page to 02 (0x200) 
030B	EI              
030C	HALT            ; wait for CTC?
~~~
Comment: Writing a value of `0xE8` to port `0xC8` sets the controller's data transfer rate. Here's the formula for Xebec SASI: 

`Clock Frequency = (Input Clock Frequency) / (4 * (Value Written to Port 0xC8))`

Assuming a 20 MHz input clock frequency, this sets the data rate to 390.625 KB/s. This seems way too fast...

The interrupt table is missing from the annontated ROM source.


## HD boot code

; Initialize the Xebec S1410 controller
05AD  XOR A              ; Set the A register to zero
05AE  OUT (#12),A        ; Write the value in the A register to port 0x12, resetting the controller
05B0  DI                 ; Disable interrupts
05B1  LD A,#47           ; Load the A register with the value 0x47
05B3  OUT (#C8),A       ; Write the value in the A register to port 0xC8, setting the data transfer rate for the controller
05B5  LD A,#FF           ; Load the A register with the value 0xFF
05B7  OUT (#C8),A       ; Write the value in the A register to port 0xC8 again, possibly to latch the data transfer rate value
05B9  LD A,#C7           ; Load the A register with the value 0xC7
05BB  OUT (#C9),A       ; Write the value in the A register to port 0xC9, setting up the DMA controller
05BD  LD A,#50           ; Load the A register with the value 0x50
05BF  OUT (#C9),A       ; Write the value in the A register to port 0xC9 again, possibly to set up the DMA channel or other related configuration details
05C1  EI                 ; Enable interrupts
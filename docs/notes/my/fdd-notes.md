# Notes from 21. mar 2021

## Floppy drive parameters

Floppy disk size is 672768 bytes (146 tracks, 18 sectors, 256 bytes each). 

**TODO: is following info for HDD or FDD?**
First two tracks (9216 bytes) are reserved for CPMLDR, followed by 2 blocks 
of directory each block is 8 sectors i.e. 2048 bytes).

## The floppy controller is Intel 8272A

### Ports

Here are the Intel 8272 registers:

F0 ... FDC status
F1 ... FDC data
C0 ... DMA reg
E8 ... RDC interrupt vector

NEC µPD765A/B controller:

    Port 0xF0: FDC status port
    Port 0xF1: FDC data port
    Port 0xC0: DMA register
    Port 0xE8: RDC interrupt vector

f the ports for the NEC µPD765A/B controller are mapped to the 0xF0 - 0xF7 address space, the correct port numbers would be as follows:

    Port 0xF0: FDC status port
    Port 0xF1: FDC data port
    Port 0xF2: Digital Output Register (DOR)
    Port 0xF3: Main Status Register (MSR)
    Port 0xF4: Data Register (FDC data port)
    Port 0xF5: Configuration Control Register (CCR)
    Port 0xF6: Reserved
    Port 0xF7: Reserved

Note that the specific port numbers used to access the controller may depend on the specific system and software configuration being used. Some systems may use different port mappings or address spaces for the FDC, and the specific port numbers used may vary accordingly.

However, the mapping described above is the default mapping used by many systems and should be valid for many cases. The port numbers listed above correspond to the hexadecimal values 0xF0 through 0xF7, which are commonly used for the FDC on many systems.

### Status

Bit 7: Seek End (SE) - Set when the FDC has completed a seek operation to the desired track.
Bit 6: Write Protect (WP) - Set when the floppy disk in the selected drive is write-protected.
Bit 5: Track 0 (T0) - Set when the read/write head of the floppy disk drive is positioned at track 0.
Bit 4: Double-Density (DD) - Set when the data on the floppy disk is stored in double density format.
Bit 3: Drive Ready (DR) - Set when the selected floppy disk drive is ready to perform read or write operations.
Bit 2: End of Cylinder (EC) - Set when the read/write head of the floppy disk drive reaches the end of the current cylinder.
Bit 1: Motor On (MO) - Set when the motor that spins the floppy disk is currently on.
Bit 0: Busy (BSY) - Set when the FDC is currently executing a command and is not ready to receive new commands.

## Analyse this:



LD	A,#08
;; 0x337
PUSH	AF
loop:
IN	A,(#F0)     ; read FDC status
AND	#C0         ; Only interested in bit 7
CP	#80         ; if ongoing SE, wait...
JP	NZ,loop     ; wait until SE (seek operation to track)
POP	AF
;; at this point we know we are on correct track
OUT	(#F1),A     ; ??
RET

;; weird method for waiting for FDC to be ready to
;; transfer data and then read 1 byte into A?
0345	IN	A,(#F0) ; read status
0347	AND	#C0
0349	CP	#C0     ; tests for SE and write protect bits?
034B	JP	NZ,#0345
034E	IN	A,(#F1) ; read data port...?
0350	RET
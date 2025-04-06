In the case of the Z80 DMA chip, the DMA port space from 0xC0 to 0xC7 is a range of 8 consecutive I/O ports that are used to program and control the DMA chip.

Here is a brief overview of what each of these 8 ports is typically used for in a Z80 DMA controller:

    Port 0xC0: Mode set register. This port is used to configure the DMA controller's transfer mode. The transfer mode determines how data is transferred between memory and I/O devices. The specific configuration of this register will depend on the DMA mode being used (e.g., single transfer mode or block transfer mode).

    Port 0xC1: Memory page register. This port is used to set the high-order 8 bits of the 16-bit starting address of the source or destination memory block.

    Port 0xC2: Low-order source address register. This port is used to set the low-order 8 bits of the starting address of the source memory block.

    Port 0xC3: High-order source address register. This port is used to set the high-order 8 bits of the starting address of the source memory block.

    Port 0xC4: Low-order destination address register. This port is used to set the low-order 8 bits of the starting address of the destination memory block.

    Port 0xC5: High-order destination address register. This port is used to set the high-order 8 bits of the starting address of the destination memory block.

    Port 0xC6: Low-order byte count register. This port is used to set the low-order 8 bits of the byte count for the data transfer.

    Port 0xC7: High-order byte count register. This port is used to set the high-order 8 bits of the byte count for the data transfer.

Note that the specific usage of each port may vary depending on the configuration of the DMA controller and the particular requirements of your system. You should consult the documentation for your specific Z80 DMA controller to get a more detailed understanding of how to program and control the DMA chip using these I/O ports.


    Port 0x00: Command/Status register
    Port 0x01: Channel 0 address register (low byte)
    Port 0x02: Channel 0 address register (high byte)
    Port 0x03: Channel 0 word count register (low byte)
    Port 0x04: Channel 0 word count register (high byte)
    Port 0x05: Channel 1 address register (low byte)
    Port 0x06: Channel 1 address register (high byte)
    Port 0x07: Channel 1 word count register (low byte)
    Port 0x08: Channel 1 word count register (high byte)




LD HL,#dma_config_1
LD C,#0xC0
LD B,#0x11
OTIR
dma_config_1:
.db 0xC3, 0x05, 0xCF, 0x79, 0x00, 0xE0, 0xFF, 0x11, 0x14, 0x28, 0x85, 0xF1, 0x8A, 0xCF, 0x01, 0xCF, 0x87   

0742   053F C3 05 CF 
0743   0542 79                 
0744   0543 00                 
0745   0544 E0                 
0746   0545 FF                
0747   0546 11 14 28         
0748   0549 85                  
0749   054A F1                  
0750   054B 8A                  
0751   054C CF                  
0752   054D 01 CF 87   
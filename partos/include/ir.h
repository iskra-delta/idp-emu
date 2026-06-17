/*
 * Declares the PartOS interrupt-routine interface: reference-counted
 * interrupt enable/disable and access to the IM2 vector table.
 *
 * The kernel runs the Z80 in interrupt mode 2. The I register points at a
 * page-aligned, 512-byte vector table (KERNEL_IM2_BASE). When a peripheral
 * acknowledges an interrupt it places an 8-bit vector on the bus, and the CPU
 * reads the 2-byte handler address from (I << 8) | vector. So a "vector
 * number" here is exactly that bus byte (0..0xFF), and its handler slot lives
 * at KERNEL_IM2_BASE + vector. Real peripherals emit even, spaced vectors
 * (e.g. the CTC channels emit base, base+2, base+4, base+6).
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef IR_H
#define IR_H

#include <stdint.h>

/*
 * Initialize interrupt routines: clear the disable nesting count and point
 * the I register at the kernel IM2 vector page. IM2 is not selected and
 * interrupts are not enabled here.
 */
void ir_init(void);

/*
 * Disable interrupts with nesting (reference-counted di).
 */
void ir_disable(void);

/*
 * Enable interrupts with nesting (reference-counted ei); the matching
 * ir_enable for the outermost ir_disable re-enables interrupts.
 */
void ir_enable(void);

/*
 * Install an IM2 handler for the given vector number (0..0xFF) and return the
 * previous handler.
 */
void *ir_set(uint8_t vector, void *handler);

#endif /* IR_H */

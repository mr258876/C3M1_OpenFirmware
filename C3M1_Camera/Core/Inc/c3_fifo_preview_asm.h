/* v0.1.40: independent preview-only Thumb-2 instruction experiments.
 * Not included in the capture/BMP/DNG FIFO path. Both routines retain the
 * board-verified order BRR(PB4 low) -> IDR(PG1..PG8) -> BSRR(PB4 high).
 * GPIO reads/writes occur in assembly with a memory clobber to prevent the
 * compiler from crossing the volatile peripheral accesses. Never run with
 * the untested TIM3/FIFO DMA arrangement. Field validation is mandatory.
 */
#ifndef C3_FIFO_PREVIEW_ASM_H
#define C3_FIFO_PREVIEW_ASM_H
#include <stdint.h>
#include "c3_camera.h"
#ifndef C3_FIFO_ASM_MODE
#define C3_FIFO_ASM_MODE 0
#endif
#if C3_FIFO_ASM_MODE != 0
#if !defined(__arm__) && !defined(__thumb__)
#error "FIFO preview assembly requires ARM Thumb target"
#endif
/* A1: enforce the three peripheral transactions as a single asm block.
 * Keep UBFX after the rising RCLK edge to preserve sampling semantics.
 * Applied ONLY to the optimized preview row routines when mode == 1.
 */
static inline __attribute__((always_inline)) uint8_t c3_fifo_asm_read_byte(void)
{
    uint32_t value;
    __asm__ volatile (
        "str %[mask], [%[brr]]\n\t"
        "ldr %[data], [%[idr]]\n\t"
        "str %[mask], [%[bsrr]]\n\t"
        "ubfx %[data], %[data], #1, #8\n\t"
        : [data] "=&r"(value)
        : [mask] "r"((uint32_t)GPIO_PIN_4),
          [brr] "r"((uintptr_t)&GPIOB->BRR),
          [idr] "r"((uintptr_t)&GPIOG->IDR),
          [bsrr] "r"((uintptr_t)&GPIOB->BSRR)
        : "memory");
    return (uint8_t)value;
}
/* A2: same triplet per byte, 320 pixels = 640 samples exactly. Retain one
 * SRAM STRB after each rising edge, so the FIFO sees a nonzero high phase.
 * Only the plain preview rows use this routine (not OSD / peaking / still).
 * Counter is pixels, not bytes. No interrupts are disabled.
 */
static inline __attribute__((always_inline)) void c3_fifo_asm_copy_plain(uint8_t *dst)
{
    uint32_t remaining = 320u;
    uint32_t sample;
    __asm__ volatile (
        "1:\n\t"
        "str %[mask], [%[brr]]\n\t"
        "ldr %[data], [%[idr]]\n\t"
        "str %[mask], [%[bsrr]]\n\t"
        "ubfx %[data], %[data], #1, #8\n\t"
        "strb %[data], [%[out]], #1\n\t"
        "str %[mask], [%[brr]]\n\t"
        "ldr %[data], [%[idr]]\n\t"
        "str %[mask], [%[bsrr]]\n\t"
        "ubfx %[data], %[data], #1, #8\n\t"
        "strb %[data], [%[out]], #1\n\t"
        "subs %[cnt], %[cnt], #1\n\t"
        "bne 1b\n\t"
        : [out] "+&r"(dst), [cnt] "+&r"(remaining),
          [data] "=&r"(sample)
        : [mask] "r"((uint32_t)GPIO_PIN_4),
          [brr] "r"((uintptr_t)&GPIOB->BRR),
          [idr] "r"((uintptr_t)&GPIOG->IDR),
          [bsrr] "r"((uintptr_t)&GPIOB->BSRR)
        : "cc", "memory");
}
#endif
#endif

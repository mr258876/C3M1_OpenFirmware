/* v0.1.93 preview-only FIFO readers. The GPIO triplet and read count
 * are unchanged. Use low registers to emit 16-bit Thumb GPIO instructions.
 * No timer/DMA changes, interrupt masking, or changes to the still reader.
 * RCLK-high has at least two CPU instructions before the next low; actual
 * electrical timings still need validation on the board.
 */
#ifndef C3_FIFO_PREVIEW_LOWREG_H
#define C3_FIFO_PREVIEW_LOWREG_H
#include <stdint.h>
#include <stddef.h>
#include "c3_camera.h"
#ifndef C3_FIFO_ROW_ARM
#define C3_FIFO_ROW_ARM 0
#endif
#if C3_FIFO_ROW_ARM
#if !defined(__arm__) && !defined(__thumb__)
#error "low-register FIFO readers require ARM Thumb"
#endif
_Static_assert(offsetof(GPIO_TypeDef, BRR)==20u && offsetof(GPIO_TypeDef, BSRR)==16u && offsetof(GPIO_TypeDef, IDR)==8u,"unexpected GPIO offsets");
_Static_assert(GPIO_PIN_4==16u,"unexpected RCLK pin");
#define C3_LOWREG_TRIPLET(reg) \
    "str %[mask], [%[pb], #20]\n\t" \
    "ldr %[" reg "], [%[pg], #8]\n\t" \
    "str %[mask], [%[pb], #16]\n\t"
#if C3_FIFO_ROW_ARM == 1
/* Packed halfword: fixed 4-pixel groups. Use UBFX and NOP after the first
 * sample to avoid a back-to-back BSRR/BRR pair. BFI joins two bytes. */
#define C3_LOWREG_PAIR(off) \
    C3_LOWREG_TRIPLET("a") \
    "ubfx %[a], %[a], #1, #8\n\t" \
    "nop\n\t" \
    C3_LOWREG_TRIPLET("b") \
    "lsrs %[b], %[b], #1\n\t" \
    "bfi %[a], %[b], #8, #8\n\t" \
    "strh %[a], [%[out], #" off "]\n\t"
static __attribute__((noinline)) void c3_fifo_lowreg_row(uint8_t *out)
{
    uint32_t n=80u,a,b;
    __asm__ volatile(
        "1:\n\t"
        C3_LOWREG_PAIR("0") C3_LOWREG_PAIR("2")
        C3_LOWREG_PAIR("4") C3_LOWREG_PAIR("6")
        "adds %[out], #8\n\t"
        "subs %[n], #1\n\t"
        "bne 1b\n\t"
        : [out] "+&l"(out),[n] "+&l"(n),[a] "=&l"(a),[b] "=&l"(b)
        : [pb] "l"((uintptr_t)GPIOB),[pg] "l"((uintptr_t)GPIOG),[mask] "l"((uint32_t)GPIO_PIN_4)
        : "cc","memory");
}
#undef C3_LOWREG_PAIR
#elif C3_FIFO_ROW_ARM == 2
/* 8 byte group, same shift and byte store after every rising edge as
 * the old renderer. Low register/offset encoding gives 5 narrow Thumb
 * instructions per byte; fewer instruction fetch bytes, not fewer RCLKs. */
#define C3_LOWREG_BYTE(off) \
    C3_LOWREG_TRIPLET("a") \
    "lsrs %[a], %[a], #1\n\t" \
    "strb %[a], [%[out], #" off "]\n\t"
static __attribute__((noinline)) void c3_fifo_lowreg_row(uint8_t *out)
{
    uint32_t n=80u,a;
    __asm__ volatile(
        "1:\n\t"
        C3_LOWREG_BYTE("0") C3_LOWREG_BYTE("1")
        C3_LOWREG_BYTE("2") C3_LOWREG_BYTE("3")
        C3_LOWREG_BYTE("4") C3_LOWREG_BYTE("5")
        C3_LOWREG_BYTE("6") C3_LOWREG_BYTE("7")
        "adds %[out], #8\n\t"
        "subs %[n], #1\n\t"
        "bne 1b\n\t"
        : [out] "+&l"(out),[n] "+&l"(n),[a] "=&l"(a)
        : [pb] "l"((uintptr_t)GPIOB),[pg] "l"((uintptr_t)GPIOG),[mask] "l"((uint32_t)GPIO_PIN_4)
        : "cc","memory");
}
#undef C3_LOWREG_BYTE
#else
#error invalid low-register FIFO variant
#endif
#undef C3_LOWREG_TRIPLET
#endif
#endif

#ifndef C3_DMA_UNPACK_H
#define C3_DMA_UNPACK_H
#include "c3_fifo_dma_live_plan.h"
#ifndef C3_DMA_UNPACK
#define C3_DMA_UNPACK 0
#endif
#if C3_DMA_UNPACK < 0 || C3_DMA_UNPACK > 3
#error invalid DMA unpack mode
#endif
_Static_assert(C3_FD_BYTES==640u,"QVGA row geometry");
/* Aligned rows; memcpy preserves aliasing rules; LCD MSB,LSB byte order. */
static inline uint32_t c3_unpack_two(uint32_t a,uint32_t b)
{
    return (uint32_t)c3_live_unpack_pair(a)|((uint32_t)c3_live_unpack_pair(b)<<16u);
}
static inline void c3_unpack_pack32(uint8_t *dst,const uint16_t *src)
{
    dst=__builtin_assume_aligned(dst,4);src=__builtin_assume_aligned(src,4);
#if defined(__clang__)
#pragma clang loop unroll_count(4)
#endif
    for(unsigned i=0;i<C3_FD_BYTES;i+=4u){
        uint32_t a,b,q;
        __builtin_memcpy(&a,src+i,4);__builtin_memcpy(&b,src+i+2,4);
        q=c3_unpack_two(a,b);__builtin_memcpy(dst+i,&q,4);
    }
}
#if C3_DMA_UNPACK==2 && defined(__arm__) && defined(__thumb__)
/* AAPCS r0=dst,r1=src. Leaf, preserves r4-r7. Four pixels per iteration. */
__attribute__((naked,noinline)) static void c3_unpack_asm(uint8_t *dst,const uint16_t *src)
{
    __asm volatile(
        "push {r4-r7}\n" "movs r7,#80\n" "1: ldmia r1!,{r2-r5}\n"
        "lsrs r6,r2,#17\n" "lsrs r2,r2,#1\n" "bfi r2,r6,#8,#8\n"
        "lsrs r6,r3,#17\n" "lsrs r3,r3,#1\n" "bfi r3,r6,#8,#8\n" "bfi r2,r3,#16,#16\n"
        "lsrs r6,r4,#17\n" "lsrs r4,r4,#1\n" "bfi r4,r6,#8,#8\n"
        "lsrs r6,r5,#17\n" "lsrs r5,r5,#1\n" "bfi r5,r6,#8,#8\n" "bfi r4,r5,#16,#16\n"
        "stmia r0!,{r2,r4}\n" "subs r7,#1\n" "bne 1b\n" "pop {r4-r7}\n" "bx lr\n");
}
#else
static inline void c3_unpack_asm(uint8_t *dst,const uint16_t *src){c3_unpack_pack32(dst,src);}
#endif
static inline uint16_t c3_unpack_green_pair(uint32_t q)
{
    uint32_t z=((q&0x00070007u)<<3u)|((q>>13u)&0x00070007u);
    return (uint16_t)(z|(z>>8u));
}
static inline void c3_unpack_fused(uint8_t *dst,const uint16_t *src,uint8_t *green)
{
    dst=__builtin_assume_aligned(dst,4);src=__builtin_assume_aligned(src,4);
    green=__builtin_assume_aligned(green,4);
#if defined(__clang__)
#pragma clang loop unroll_count(4)
#endif
    for(unsigned i=0;i<C3_FD_BYTES;i+=4u){
        uint32_t a,b,q;uint16_t g;
        __builtin_memcpy(&a,src+i,4);__builtin_memcpy(&b,src+i+2,4);
        q=c3_unpack_two(a,b);g=c3_unpack_green_pair(q);
        __builtin_memcpy(dst+i,&q,4);__builtin_memcpy(green+i/2u,&g,2);
    }
}
#endif

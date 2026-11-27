/* Host contract test for v34 conservative CPU FIFO burst copy.
 * No MCU peripherals: substitute a deterministic FIFO byte source. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t pos;
static uint8_t expected(uint32_t i) { return (uint8_t)((i * 37u + (i >> 3u) + 17u) & 0xffu); }
static uint8_t next_byte(void) { assert(pos < 640u); return expected(pos++); }
#define C3_FIFO_CPU_GET_BYTE() next_byte()
#include "c3_fifo_cpu_burst.h"
int main(void)
{
    uint8_t dst[644];
    for (uint32_t i = 0u; i < 644u; ++i) dst[i] = 0xa5u;
    c3_fifo_cpu_copy_row(dst + 2u);
    assert(pos == 640u);
    assert(dst[0] == 0xa5u && dst[1] == 0xa5u && dst[642] == 0xa5u && dst[643] == 0xa5u);
    for (uint32_t i = 0u; i < 640u; ++i) assert(dst[i + 2u] == expected(i));
    puts("PASS: 640 bytes, no duplicates/skips, unchanged byte order and guards");
    return 0;
}

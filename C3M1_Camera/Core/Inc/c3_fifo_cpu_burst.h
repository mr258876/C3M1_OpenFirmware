/* v0.1.34 conservative CPU FIFO burst experiment.
 * The actual GPIO low -> read -> high sequence lives in
 * c3_camera_fifo_read_fast(): DO NOT restructure or change that function.
 * Only the loop around byte copies is unrolled.  Preview-only.
 * C3_FIFO_CPU_BURST_BYTES = 4 or 8; zero selects byte-for-byte v0.1.32 code.
 */
#ifndef C3_FIFO_CPU_BURST_H
#define C3_FIFO_CPU_BURST_H
#include <stdint.h>
#ifndef C3_FIFO_CPU_BURST_BYTES
#define C3_FIFO_CPU_BURST_BYTES 0
#endif
#if C3_FIFO_CPU_BURST_BYTES != 0 && C3_FIFO_CPU_BURST_BYTES != 4 && C3_FIFO_CPU_BURST_BYTES != 8
#error "C3_FIFO_CPU_BURST_BYTES must be 0, 4, or 8"
#endif
#if C3_FIFO_CPU_BURST_BYTES != 0
#ifndef C3_FIFO_CPU_GET_BYTE
#error "Define C3_FIFO_CPU_GET_BYTE() before including this header"
#endif
/* 320 px x RGB565 = 640 bytes; 4 and 8 both divide 640 exactly. */
#define C3_FIFO_CPU_ROW_BYTES 640u
_Static_assert(C3_FIFO_CPU_ROW_BYTES % C3_FIFO_CPU_BURST_BYTES == 0u, "non-integral bursts");
static inline __attribute__((always_inline)) void c3_fifo_cpu_copy_row(uint8_t *dst)
{
    for (uint16_t off = 0u; off < C3_FIFO_CPU_ROW_BYTES; off += C3_FIFO_CPU_BURST_BYTES) {
        dst[off + 0u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
        dst[off + 1u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
        dst[off + 2u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
        dst[off + 3u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
#if C3_FIFO_CPU_BURST_BYTES == 8
        dst[off + 4u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
        dst[off + 5u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
        dst[off + 6u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
        dst[off + 7u] = (uint8_t)C3_FIFO_CPU_GET_BYTE();
#endif
    }
}
#endif /* C3_FIFO_CPU_BURST_BYTES */
#endif /* C3_FIFO_CPU_BURST_H */

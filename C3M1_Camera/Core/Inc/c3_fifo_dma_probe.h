#ifndef C3_FIFO_DMA_PROBE_H
#define C3_FIFO_DMA_PROBE_H
#include <stdint.h>
#ifndef C3_FIFO_DMA_PROBE
#define C3_FIFO_DMA_PROBE 0
#endif
#if C3_FIFO_DMA_PROBE
/* Bench-only frozen frame sweep. Never mounts or writes SD/settings. */
void c3_fifo_dma_probe_run(uint8_t camera_ok) __attribute__((noreturn));
#endif
#endif

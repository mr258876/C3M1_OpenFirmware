#ifndef C3_FIFO_PROBE_H
#define C3_FIFO_PROBE_H
#include <stdint.h>
/* Read-only color-bar / FIFO hardware diagnostic: never saves to SD or Flash. */
void c3_fifo_probe_run(uint8_t camera_ok);
extern volatile uint32_t c3_fp_frame;
extern volatile uint32_t c3_fp_unique;
extern volatile uint32_t c3_fp_zero_bytes;
extern volatile uint32_t c3_fp_ff_bytes;
extern volatile uint32_t c3_fp_first4;
extern volatile uint32_t c3_fp_vsync;
extern volatile uint32_t c3_fp_error;
extern volatile uint32_t c3_fp_com3;
extern volatile uint32_t c3_fp_mode;
#endif

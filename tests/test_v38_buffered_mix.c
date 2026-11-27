/* Diagnostic buffered compositor must preserve exact RGB565+OSD output.
 * No STM32 peripheral access; tests a deterministic synthetic FIFO stream.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static uint8_t fifo_data[640];
static unsigned fifo_index;
static uint8_t fake_fifo_read(void) {
    assert(fifo_index < 640u);
    return fifo_data[fifo_index++];
}
static uint16_t dim565(uint16_t c) { return (uint16_t)((c & 0xF7DEu) >> 1); }
#define C3_ROW_FIFO_BYTE() fake_fifo_read()
#define C3_ROW_DIM565(v) dim565(v)
#include "../C3M1_Camera/Core/Inc/c3_preview_row_opt.h"
#include "../C3M1_Camera/Core/Inc/c3_row_profile_mix.h"
static uint32_t rng_state = 0x13579BDFu;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(void) {
    uint8_t mask[40], raw[642], expected[642], actual[642];
    for (unsigned iteration=0; iteration<5000u; ++iteration) {
        for (unsigned i=0; i<640u; ++i) fifo_data[i]=(uint8_t)rnd();
        for (unsigned i=0; i<40u; ++i) {
            if (iteration==0) mask[i]=0;
            else if (iteration==1) mask[i]=0xFF;
            else if (iteration==2) mask[i]=0xAA;
            else if (iteration==3) mask[i]=0x55;
            else if (iteration%7==0) mask[i] = (uint8_t)((rnd()%4==0)? rnd():0);
            else mask[i] = (uint8_t)rnd();
        }
        fifo_index=0;
        memset(expected, 0xD3, sizeof(expected));
        c3_row_copy_osd(expected, mask); /* Field-tested mainline RowOpt2 */
        assert(fifo_index==640u && expected[640]==0xD3 && expected[641]==0xD3);
        fifo_index=0;
        memset(raw, 0xAB, sizeof(raw));
        c3_row_copy_plain(raw);          /* Same 640 FIFO reads -> scratch */
        assert(fifo_index==640u && raw[640]==0xAB && raw[641]==0xAB);
        memset(actual, 0xE7, sizeof(actual));
        c3_row_profile_mix_osd(actual, raw, mask);
        assert(actual[640]==0xE7 && actual[641]==0xE7);
        assert(memcmp(expected, actual, 640u)==0);
        assert(memcmp(raw, fifo_data, 640u)==0);
    }
    puts("PASS: buffered diagnostic vs RowOpt2: 5000 masks/rows, 640 bytes each, no overflow");
    return 0;
}

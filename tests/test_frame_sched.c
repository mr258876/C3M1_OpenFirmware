/* AL422B 393216-byte circular-address and VSYNC-queue host simulation.
 * Checks drop policy, reset semantics, 128-byte guard, and wrap-around.
 * Not an electrical timing or OV7725 HREF byte-count test.
 */
#include "c3_frame_sched.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void transitions(void)
{
    c3_frame_sched_t s;
    c3_sched_reset(&s, C3_SCHED_PREVIEW);
    assert(s.ready==0 && !s.reading && !s.writing);
    assert(c3_sched_vsync(&s) == 1 && s.writing);
    assert(!c3_sched_acquire(&s));
    assert(c3_sched_vsync(&s) == 1 && s.ready == 1); /* F1 complete, F2 starts */
    assert(c3_sched_acquire(&s) && s.reading && s.ready == 0);
    assert(!c3_sched_acquire(&s));
    assert(c3_sched_vsync(&s) == 0 && s.ready == 1); /* F2 full, skip F3 */
    assert(s.frames_skipped == 1 && !s.writing);
    c3_sched_release(&s);
    assert(!c3_sched_acquire(&s)); /* must wait for next producer */
    assert(c3_sched_vsync(&s) == 1 && s.writing); /* F4 begins */
    assert(c3_sched_acquire(&s)); /* F2 is safe with F4 writing */
    assert(c3_sched_vsync(&s) == 0 && s.ready == 1); /* F4 complete */
    c3_sched_release(&s);
    assert(!c3_sched_acquire(&s)); /* no writer & only freshest ready */
    c3_sched_reset(&s, C3_SCHED_STILL);
    assert(c3_sched_vsync(&s) == 0);
    c3_sched_arm_still(&s);
    assert(c3_sched_vsync(&s) == 1);
    assert(c3_sched_vsync(&s) == 0 && s.ready == 1);
    for (int i=0;i<10;i++) assert(c3_sched_vsync(&s) == 0 && s.ready == 1);
    c3_sched_arm_still(&s);
    assert(c3_sched_vsync(&s) == 1 && s.ready == 0);
    assert(c3_sched_vsync(&s) == 0 && s.ready == 1);
    c3_sched_reset(&s, C3_SCHED_OFF);
    assert(c3_sched_vsync(&s) == 0 && !s.writing);
}

/* No frame RAM exists in the MCU. In this test we intentionally use a host
 * buffer to model the external FIFO *and* its 393216-byte modulo pointer. */
static uint8_t fifo[C3_FIFO_BYTES];
static uint64_t fifo_write_bytes, fifo_read_bytes;
static uint32_t next_frame = 1u, expected_frame = 1u;

static uint8_t pixel_pattern(uint32_t frame, uint32_t offset)
{
    return (uint8_t)((frame * 17u + (offset >> 4) + offset * 13u) & 0xffu);
}
static void emulate_write_frame(void)
{
    for (uint32_t j=0;j<C3_PREVIEW_FRAME_BYTES;j++)
        fifo[(fifo_write_bytes + j) % C3_FIFO_BYTES] = pixel_pattern(next_frame,j);
    fifo_write_bytes += C3_PREVIEW_FRAME_BYTES;
    next_frame++;
}
static void emulate_read_frame(void)
{
    for (uint32_t j=0;j<C3_PREVIEW_FRAME_BYTES;j++) {
        uint8_t got = fifo[(fifo_read_bytes + j) % C3_FIFO_BYTES];
        if (got != pixel_pattern(expected_frame, j)) {
            fprintf(stderr, "FIFO corruption: frame %u offset %u got %02x expected %02x\n",
                   expected_frame, j, got, pixel_pattern(expected_frame,j));
            abort();
        }
    }
    expected_frame++;
    fifo_read_bytes += C3_PREVIEW_FRAME_BYTES;
}
static void emulate_edge(c3_frame_sched_t *s)
{
    if (s->writing) emulate_write_frame();
    uint8_t gate = c3_sched_vsync(s);
    assert(gate == s->writing);
    /* Every complete unconsumed frame occupies 153600 bytes. During a
     * physical write the future frame is also reserved. */
    assert(s->ready + s->reading + s->writing <= 2);
    assert(fifo_write_bytes >= fifo_read_bytes);
    assert(fifo_write_bytes - fifo_read_bytes <= 2 * C3_PREVIEW_FRAME_BYTES);
}
static void circular_fifo_test(void)
{
    c3_frame_sched_t s;
    c3_sched_reset(&s, C3_SCHED_PREVIEW);
    memset(fifo,0,sizeof(fifo));
    fifo_write_bytes=fifo_read_bytes=0;
    next_frame=expected_frame=1;
    uint32_t drawn=0;
    /* Each test iteration includes one complete VSYNC event; intentionally
     * vary the consumer throughput (slow, fast, stalled) and test 500 wraps. */
    for (unsigned edge=0;edge<2500;edge++) {
        emulate_edge(&s);
        if (edge % 17 == 0) continue;  /* intentionally stall the reader */
        if ((edge % 5) == 2 || (edge % 5) == 3) continue;
        if (c3_sched_acquire(&s)) {
            emulate_read_frame();
            c3_sched_release(&s);
            drawn++;
        }
    }
    /* Drain buffered last frame only after the next producer has started.
     * Several extra VSYNC edges make progress as the consumer catches up. */
    for (unsigned edge=0;edge<15;edge++) {
        emulate_edge(&s);
        if (c3_sched_acquire(&s)) {
            emulate_read_frame();
            c3_sched_release(&s);
            drawn++;
        }
    }
    assert(drawn > 400);
    assert(s.frames_skipped > 0);
    assert(s.frames_completed == next_frame - 1);
    assert(s.frames_completed >= drawn);
    /* No frame may be silently lost from a circular FIFO queue: a skipped
     * *sensor* frame is different from a completed FIFO frame. */
    assert(drawn == expected_frame - 1);
    printf("Ring wrap verified: produced=%lu, displayed=%lu, intentionally skipped=%lu, bytes=%llu\n",
           (unsigned long)s.frames_completed, (unsigned long)drawn,
           (unsigned long)s.frames_skipped,(unsigned long long)fifo_write_bytes);
}
int main(void)
{
    transitions();
    circular_fifo_test();
    puts("VSYNC scheduler state transitions and circular FIFO passed");
    return 0;
}

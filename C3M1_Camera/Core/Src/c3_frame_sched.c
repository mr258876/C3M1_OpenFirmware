#include "c3_frame_sched.h"

void c3_sched_reset(c3_frame_sched_t *s, c3_sched_mode_t mode)
{
    s->mode = (uint8_t)mode;
    s->armed = (uint8_t)(mode == C3_SCHED_PREVIEW);
    s->writing = 0u;
    s->ready = 0u;
    s->reading = 0u;
    s->edges = 0u;
    s->frames_completed = 0u;
    s->frames_skipped = 0u;
}

void c3_sched_arm_still(c3_frame_sched_t *s)
{
    s->mode = C3_SCHED_STILL;
    s->armed = 1u;
    s->writing = 0u;
    s->ready = 0u;
    s->reading = 0u;
}

uint8_t c3_sched_vsync(c3_frame_sched_t *s)
{
    if (s->mode == C3_SCHED_OFF) return 0u;
    s->edges++;
    if (s->writing) {
        /* The frame ended. WEN must be disabled at this boundary before
         * software publishes the newly completed FIFO frame. */
        s->writing = 0u;
        s->ready++;
        s->frames_completed++;
    }
    if (s->mode == C3_SCHED_STILL) {
        if (s->armed && s->ready == 0u) {
            s->armed = 0u;
            s->writing = 1u;
        }
        return s->writing;
    }
    /* The frame in the reader counts as in-flight. Never have more than
     * TWO 153600-byte frames outstanding (completed + reader + writer).
     * We can disable WE for skipped frames WITHOUT moving the write pointer.
     */
    if ((uint8_t)(s->ready + s->reading) < 2u) {
        s->writing = 1u;
    } else {
        s->frames_skipped++;
    }
    return s->writing;
}

uint8_t c3_sched_acquire(c3_frame_sched_t *s)
{
    if (s->mode != C3_SCHED_PREVIEW || s->reading || s->ready == 0u)
        return 0u;
    /* The newest frame may still be in AL422B's 128-byte input staging
     * buffer. Do not read it if no newer frame has begun writing. Reading
     * an older frame while a newer one is queued also keeps >=153600-byte
     * lead in the circular FIFO, well beyond the 128-byte minimum.
     */
    if (!s->writing && s->ready < 2u) return 0u;
    --s->ready;
    s->reading = 1u;
    return 1u;
}

void c3_sched_release(c3_frame_sched_t *s)
{
    s->reading = 0u;
}

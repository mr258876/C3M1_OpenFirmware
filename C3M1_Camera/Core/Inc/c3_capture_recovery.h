#ifndef C3_CAPTURE_RECOVERY_H
#define C3_CAPTURE_RECOVERY_H

#include <stdint.h>
#include "c3_storage.h"

/* Pure application state: a captured RAW8/RGB565 frame must not be released until
 * either the image was fully committed or the user explicitly discards it. */
typedef enum {
    C3_RECOVERY_IDLE = 0,
    C3_RECOVERY_HELD = 1,
    C3_RECOVERY_FAILED = 2
} c3_recovery_phase_t;

typedef struct {
    c3_recovery_phase_t phase;
    c3_save_result_t last_result;
} c3_capture_recovery_t;

void c3_recovery_reset(c3_capture_recovery_t *s);
void c3_recovery_captured(c3_capture_recovery_t *s);
void c3_recovery_save_finished(c3_capture_recovery_t *s, c3_save_result_t result);
void c3_recovery_discard(c3_capture_recovery_t *s);
uint8_t c3_recovery_can_retry(const c3_capture_recovery_t *s);
uint8_t c3_recovery_has_frame(const c3_capture_recovery_t *s);

#endif

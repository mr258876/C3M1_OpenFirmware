#include "c3_capture_recovery.h"

void c3_recovery_reset(c3_capture_recovery_t *s)
{
    s->phase = C3_RECOVERY_IDLE;
    s->last_result = C3_SAVE_OK;
}
void c3_recovery_captured(c3_capture_recovery_t *s)
{
    s->phase = C3_RECOVERY_HELD;
    s->last_result = C3_SAVE_OK;
}
void c3_recovery_save_finished(c3_capture_recovery_t *s, c3_save_result_t result)
{
    if (s->phase == C3_RECOVERY_IDLE) return;
    s->last_result = result;
    s->phase = (result == C3_SAVE_OK) ? C3_RECOVERY_IDLE : C3_RECOVERY_FAILED;
}
void c3_recovery_discard(c3_capture_recovery_t *s)
{
    c3_recovery_reset(s);
}
uint8_t c3_recovery_can_retry(const c3_capture_recovery_t *s)
{
    return (uint8_t)(s->phase == C3_RECOVERY_FAILED);
}
uint8_t c3_recovery_has_frame(const c3_capture_recovery_t *s)
{
    return (uint8_t)(s->phase != C3_RECOVERY_IDLE);
}

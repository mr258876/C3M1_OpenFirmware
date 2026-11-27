#ifndef C3_PLAYBACK_H
#define C3_PLAYBACK_H
#include <stdint.h>
typedef struct {uint32_t key;uint8_t info;} c3_playback_t;
/* Only called in the modal page with FIFO stopped and no held still.
 * Return 0 for SD I/O failures, 1 for an accessible volume (even empty
 * or a corrupt/unsupported image). No writes to the card. */
uint8_t c3_playback_enter(c3_playback_t *s);
uint8_t c3_playback_step(c3_playback_t *s,int movement);
uint8_t c3_playback_toggle_info(c3_playback_t *s);
#include "c3_settings.h"
/* Directly open the just-committed photo, without a directory scan or remount. */
uint8_t c3_playback_show_saved(uint32_t number,c3_output_format_t format);
#endif

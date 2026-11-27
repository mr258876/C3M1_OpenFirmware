#ifndef C3_STORAGE_H
#define C3_STORAGE_H
#include <stdint.h>
#include "c3_settings.h"
typedef enum {
    C3_SAVE_OK=0, C3_SAVE_NO_SD, C3_SAVE_OPEN_ERROR,
    C3_SAVE_WRITE_ERROR, C3_SAVE_SYNC_ERROR,
    C3_SAVE_CLOSE_ERROR, C3_SAVE_NO_SPACE,
    C3_SAVE_RENAME_ERROR, C3_SAVE_FIFO_ERROR
} c3_save_result_t;
uint8_t c3_storage_mount(void);
typedef enum { C3_FORMAT_OK=0, C3_FORMAT_NOT_READY, C3_FORMAT_TOO_LARGE,
               C3_FORMAT_FAILED, C3_FORMAT_MOUNT_ERROR } c3_format_result_t;
c3_format_result_t c3_storage_format_card(void);
/* Mark volume stale after a read-only playback I/O failure. */
void c3_storage_invalidate(void);
uint32_t c3_storage_next_number(void);
c3_save_result_t c3_storage_save(c3_output_format_t format);
/* Compatibility with existing DNG host tests / older caller code. */
#endif

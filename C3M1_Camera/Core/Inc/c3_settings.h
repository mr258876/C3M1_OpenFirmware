#ifndef C3_SETTINGS_H
#define C3_SETTINGS_H
#include <stdint.h>
/* All fields are single bytes; this structure is persisted with a versioned
 * CRC-protected record. Never write it as individual naked Flash bytes. */
typedef struct {
    uint8_t peaking;
    uint8_t color;           /* 0 red, 1 yellow, 2 cyan, 3 green */
    uint8_t sensitivity;     /* 0 low, 1 medium, 2 high */
    uint8_t focus_score;     /* C3_SCORE_*: OFF / FULL SP */
    uint8_t fps;
    uint8_t grid;
    uint8_t monochrome;
    uint8_t osd;
    uint8_t output_format; /* C3_OUTPUT_DNG / C3_OUTPUT_BMP */
    uint8_t rotate_180;    /* 1 = LCD 180-degree rotation (default ON) */
    uint8_t peak_area;     /* v6: low 3 bits language/hint; v219 bit3 review OFF, bit4 wind OFF */
    uint8_t sleep_timer;    /* 0=OFF, 1=15s, 2=30s, 3=1min, 4=3min */
} c3_settings_t;

enum {
    C3_PEAK_AREA_CENTER = 0,
    C3_PEAK_AREA_FULL = 1,
    C3_PEAK_AREA_CENTER_SPARSE = 2,
    C3_PEAK_AREA_FULL_SPARSE = 3,
    C3_PEAK_AREA_COUNT = 4
};

#define C3_LANGUAGE_ZH_VALUE 4u
#define C3_UI_PREF_MASK 7u
#define C3_POST_REVIEW_OFF 8u
#define C3_POST_WIND_OFF 16u
static inline uint8_t c3_settings_review(const c3_settings_t *s) { return !(s->peak_area & C3_POST_REVIEW_OFF); }
static inline uint8_t c3_settings_wind(const c3_settings_t *s) { return !(s->peak_area & C3_POST_WIND_OFF); }
static inline uint8_t c3_settings_language(const c3_settings_t *s)
{
    return (uint8_t)((s->peak_area & C3_UI_PREF_MASK) == C3_LANGUAGE_ZH_VALUE
                     || (s->peak_area & C3_UI_PREF_MASK) == 6u
                     );
}

/* No layout/version change. Pending is a committed reset/default record,
 * not a selected language. All prior v6 values remain valid. */
#define C3_LANGUAGE_PENDING_VALUE 7u
static inline uint8_t c3_settings_language_pending(const c3_settings_t *s)
{
    return (uint8_t)((s->peak_area & C3_UI_PREF_MASK) == C3_LANGUAGE_PENDING_VALUE);
}
static inline uint8_t c3_settings_startup_hint(const c3_settings_t *s)
{
    return (uint8_t)((s->peak_area & C3_UI_PREF_MASK) <= C3_LANGUAGE_ZH_VALUE
                     || c3_settings_language_pending(s)
                     );
}
/* Preserve both preferences without changing the v6 CRC-protected layout. */
static inline void c3_settings_set_ui_prefs(c3_settings_t *s, uint8_t zh, uint8_t hint)
{
    s->peak_area = (uint8_t)((s->peak_area & (uint8_t)~C3_UI_PREF_MASK) |
                          (hint ? (zh ? 4u : 3u) : (zh ? 6u : 5u)));
}

static inline uint8_t c3_settings_peak_area(const c3_settings_t *s)
{
    (void)s;
    return C3_PEAK_AREA_FULL_SPARSE;
}

typedef enum {
    C3_OUTPUT_DNG = 0,
    C3_OUTPUT_BMP = 1
} c3_output_format_t;

enum {
    C3_SETTING_PEAK = 0,
    C3_SETTING_COLOR,
    C3_SETTING_SENSITIVITY,
    C3_SETTING_SCORE,
    C3_SETTING_FPS,
    C3_SETTING_GRID,
    C3_SETTING_MONO,
    C3_SETTING_OSD,
    C3_SETTING_OUTPUT,
    C3_SETTING_ROTATION,
    C3_SETTING_SLEEP_TIMER,
    C3_SETTING_LANGUAGE,
    C3_SETTING_STARTUP_HINT,
    C3_SETTING_REVIEW,
    C3_SETTING_WIND,
    C3_SETTING_RESET,
    C3_SETTING_FORMAT,
    C3_SETTING_ABOUT,
    C3_SETTINGS_COUNT
};

void c3_settings_defaults(c3_settings_t *s);
uint8_t c3_settings_valid(const c3_settings_t *s);
void c3_settings_next(c3_settings_t *s, uint8_t item);
const char *c3_settings_label(uint8_t item);
const char *c3_settings_value(const c3_settings_t *s, uint8_t item);
uint16_t c3_settings_color565(const c3_settings_t *s);
/* Independent of the adaptive focus threshold: a +8 edge threshold is less
 * sensitive, a -8 threshold is more sensitive. */
uint8_t c3_settings_peak_threshold(const c3_settings_t *s, uint8_t adaptive);

/* High-density STM32F103ZET6: last two 2 KiB pages of 512 KiB Flash are
 * reserved by linker_lld.ld (FLASH LENGTH=508K). This area must not
 * contain original factory data; make a full backup before first flash. */
#define C3_SETTINGS_PAGE_A 0x0807F000u
#define C3_SETTINGS_PAGE_B 0x0807F800u
#define C3_SETTINGS_MAGIC 0x43334D31u
#define C3_SETTINGS_COMMIT 0xA55Au
#define C3_SETTINGS_VERSION 6u

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    c3_settings_t data;
    uint32_t crc;
    uint16_t commit;
    uint16_t reserved;
} c3_settings_record_t;
void c3_settings_make_record(c3_settings_record_t *r,
                             const c3_settings_t *s, uint32_t sequence);
uint8_t c3_settings_record_valid(const c3_settings_record_t *r);
/* Returns 0/1 for the newer valid record or 2 if neither is valid. */
uint8_t c3_settings_select_record(const c3_settings_record_t *a,
                                  const c3_settings_record_t *b);
/* Target Flash persistence. */
uint8_t c3_settings_flash_load(c3_settings_t *out);
uint8_t c3_settings_flash_save(const c3_settings_t *settings);
/* Commit before exposing the selected language to the live settings. */
uint8_t c3_settings_flash_confirm_language(c3_settings_t *settings, uint8_t zh);
#endif

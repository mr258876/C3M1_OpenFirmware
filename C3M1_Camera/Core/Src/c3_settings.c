#include "c3_settings.h"
#include "c3_focus.h"
#include <stddef.h>
#include <string.h>

void c3_settings_defaults(c3_settings_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->color = 0u;
#if C3_DEFAULTS_V218
    s->peaking = 1u;
    s->sensitivity = 2u; /* HIGH */
    s->focus_score = C3_SCORE_FULL_SPARSE;
#else
    s->sensitivity = 1u;
    s->focus_score = C3_SCORE_OFF; /* historical defaults */
#endif
    s->osd = 1u;
    s->output_format = C3_OUTPUT_DNG;
    s->rotate_180 = 1u;
#if C3_LANGUAGE_SETUP
    s->peak_area = C3_LANGUAGE_PENDING_VALUE;
#elif C3_PEAK_FULL_SP_ONLY
    s->peak_area = C3_PEAK_AREA_FULL_SPARSE;
#else
    s->peak_area = C3_PEAK_AREA_CENTER;
#endif
#if C3_DEFAULTS_V218
    s->sleep_timer = 3u; /* 1 MIN */
#else
    s->sleep_timer = 0u; /* historical default */
#endif
}
uint8_t c3_settings_valid(const c3_settings_t *s)
{
    if (!s) return 0u;
    return (uint8_t)(s->peaking <= 1u && s->color <= 3u &&
        s->sensitivity <= 2u && s->focus_score < C3_SCORE_COUNT &&
        s->fps <= 1u && s->grid <= 1u && s->monochrome <= 1u &&
        s->osd <= 1u && s->output_format <= C3_OUTPUT_BMP &&
        s->rotate_180 <= 1u &&
#if C3_I18N
#if C3_LANGUAGE_SETUP
#if C3_POSTSHOT
        s->peak_area <= 31u &&
#else
        s->peak_area <= C3_LANGUAGE_PENDING_VALUE &&
#endif
#elif C3_STARTUP_HINT
        s->peak_area <= 6u &&
#else
        s->peak_area <= C3_LANGUAGE_ZH_VALUE &&
#endif
#else
        s->peak_area < C3_PEAK_AREA_COUNT &&
#endif
        s->sleep_timer <= 4u);
}
void c3_settings_next(c3_settings_t *s, uint8_t item)
{
    if (!s || !c3_settings_valid(s)) return;
    switch(item) {
    case C3_SETTING_PEAK: s->peaking ^= 1u; break;
#if !C3_PEAK_FULL_SP_ONLY
    case C3_SETTING_PEAK_AREA: {
        /* Keep persisted 0=CENTER, 1=FULL, but group dense/sparse in UI. */
        static const uint8_t next[] = {C3_PEAK_AREA_CENTER_SPARSE,
            C3_PEAK_AREA_FULL_SPARSE, C3_PEAK_AREA_FULL, C3_PEAK_AREA_CENTER};
        s->peak_area = next[s->peak_area]; break;
    }
#endif
    case C3_SETTING_COLOR: s->color = (uint8_t)((s->color+1u)%4u); break;
    case C3_SETTING_SENSITIVITY: s->sensitivity = (uint8_t)((s->sensitivity+1u)%3u); break;
    case C3_SETTING_SCORE:
#if C3_SCORE_FULL_SP_ONLY
        s->focus_score = s->focus_score ? C3_SCORE_OFF : C3_SCORE_FULL_SPARSE;
#else
        s->focus_score = (uint8_t)((s->focus_score+1u)%C3_SCORE_COUNT);
#endif
        break;
    case C3_SETTING_FPS: s->fps ^= 1u; break;
    case C3_SETTING_GRID: s->grid ^= 1u; break;
    case C3_SETTING_MONO: s->monochrome ^= 1u; break;
    case C3_SETTING_OSD: s->osd ^= 1u; break;
    case C3_SETTING_OUTPUT: s->output_format ^= 1u; break;
    case C3_SETTING_ROTATION: s->rotate_180 ^= 1u; break;
    case C3_SETTING_SLEEP_TIMER: s->sleep_timer = (uint8_t)((s->sleep_timer+1u)%5u); break;
#if C3_I18N
    case C3_SETTING_LANGUAGE:
#if C3_LANGUAGE_SETUP
        /* A language choice is made transactionally in the submenu. */
#elif C3_STARTUP_HINT
        c3_settings_set_ui_prefs(s,!c3_settings_language(s),c3_settings_startup_hint(s));
#else
        s->peak_area = c3_settings_language(s) ? C3_PEAK_AREA_FULL_SPARSE : C3_LANGUAGE_ZH_VALUE;
#endif
        break;
#endif
#if C3_STARTUP_HINT
    case C3_SETTING_STARTUP_HINT:
        c3_settings_set_ui_prefs(s,c3_settings_language(s),!c3_settings_startup_hint(s));
        break;
#endif
#if C3_POSTSHOT
    case C3_SETTING_REVIEW: s->peak_area ^= C3_POST_REVIEW_OFF; break;
    case C3_SETTING_WIND: s->peak_area ^= C3_POST_WIND_OFF; break;
#endif
    default: break;
    }
}
const char *c3_settings_label(uint8_t item)
{
    static const char *const labels[] = {
        "FOCUS PEAK",
#if !C3_PEAK_FULL_SP_ONLY
        "PEAK AREA",
#endif
        "PEAK COLOR", "SENSITIVITY", "FOCUS SCORE",
        "FPS DISPLAY", "GRID LINES", "MONO PREVIEW", "OSD DISPLAY",
        "SAVE FORMAT", "LCD ROTATE 180",
#if !C3_FUNCTION_MENU
        "BATTERY",
#endif
        "AUTO SLEEP"
#if C3_I18N
        , "LANGUAGE"
#endif
#if C3_STARTUP_HINT
        , "STARTUP HINT"
#endif
#if C3_MENU_ACTIONS
#if C3_POSTSHOT
        , "INSTANT REVIEW", "WIND AFTER SHOT"
#endif
        , "RESET ALL SETTINGS", "FORMAT CARD"
#endif
#if C3_FUNCTION_MENU
        , "ABOUT"
#endif
    };
    return item < C3_SETTINGS_COUNT ? labels[item] : "";
}
const char *c3_settings_value(const c3_settings_t *s, uint8_t item)
{
    static const char *const colors[] = {"RED", "YELLOW", "CYAN", "GREEN"};
    static const char *const levels[] = {"LOW", "MED", "HIGH"};
    if (!c3_settings_valid(s)) return "ERR";
    switch(item) {
    case C3_SETTING_PEAK: return s->peaking ? "ON" : "OFF";
#if !C3_PEAK_FULL_SP_ONLY
    case C3_SETTING_PEAK_AREA: {
        static const char *const modes[] = {"CENTER", "FULL", "CENTER SP", "FULL SP"};
        return modes[s->peak_area];
    }
#endif
    case C3_SETTING_COLOR: return colors[s->color];
    case C3_SETTING_SENSITIVITY: return levels[s->sensitivity];
    case C3_SETTING_SCORE: {
#if C3_SCORE_FULL_SP_ONLY
        return s->focus_score ? "ON" : "OFF";
#else
        static const char *const modes[C3_SCORE_COUNT] = {"OFF", "CENTER", "CENTER SP", "FULL", "FULL SP"};
        return modes[s->focus_score];
#endif
    }
    case C3_SETTING_FPS: return s->fps ? "ON" : "OFF";
    case C3_SETTING_GRID: return s->grid ? "ON" : "OFF";
    case C3_SETTING_MONO: return s->monochrome ? "ON" : "OFF";
    case C3_SETTING_OSD: return s->osd ? "ON" : "OFF";
    case C3_SETTING_OUTPUT: return s->output_format == C3_OUTPUT_DNG ? "RAW DNG" : "BMP";
    case C3_SETTING_ROTATION: return s->rotate_180 ? "ON" : "OFF";
#if !C3_FUNCTION_MENU
    case C3_SETTING_BATTERY: return "READ"; /* Live ADC value drawn by menu */
#endif
    case C3_SETTING_SLEEP_TIMER: {
        static const char *const choices[] = {"OFF", "15 SEC", "30 SEC", "1 MIN", "3 MIN"};
        return choices[s->sleep_timer];
    }
#if C3_I18N
    case C3_SETTING_LANGUAGE: return c3_settings_language(s) ? "SIMPLIFIED CHINESE" : "ENGLISH";
#endif
#if C3_STARTUP_HINT
    case C3_SETTING_STARTUP_HINT: return c3_settings_startup_hint(s) ? "ON" : "OFF";
#endif
#if C3_POSTSHOT
    case C3_SETTING_REVIEW: return c3_settings_review(s) ? "ON" : "OFF";
    case C3_SETTING_WIND: return c3_settings_wind(s) ? "ON" : "OFF";
#endif
    default: return "";
    }
}
uint16_t c3_settings_color565(const c3_settings_t *s)
{
    static const uint16_t rgb565[] = {0xF800u,0xFFE0u,0x07FFu,0x07E0u};
    return rgb565[c3_settings_valid(s) ? s->color : 0u];
}
uint8_t c3_settings_peak_threshold(const c3_settings_t *s, uint8_t adaptive)
{
    int threshold = adaptive;
    if (c3_settings_valid(s)) {
        if (s->sensitivity == 0u) threshold += 8;
        if (s->sensitivity == 2u) threshold -= 8;
    }
    if (threshold < 4) threshold = 4;
    if (threshold > 126) threshold = 126;
    return (uint8_t)threshold;
}

static uint32_t crc32(const uint8_t *bytes, unsigned len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (unsigned i=0;i<len;i++) {
        crc ^= bytes[i];
        for (unsigned bit=0;bit<8u;bit++)
            crc = (crc>>1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
    }
    return ~crc;
}
void c3_settings_make_record(c3_settings_record_t *r,
                             const c3_settings_t *s, uint32_t sequence)
{
    if (!r || !c3_settings_valid(s)) return;
    memset(r, 0xFF, sizeof(*r));
    r->magic = C3_SETTINGS_MAGIC;
    r->version = C3_SETTINGS_VERSION;
    r->length = sizeof(*s);
    r->sequence = sequence;
    r->data = *s;
    r->crc = crc32((const uint8_t *)r, (unsigned)offsetof(c3_settings_record_t, crc));
    r->commit = C3_SETTINGS_COMMIT;
}
uint8_t c3_settings_record_valid(const c3_settings_record_t *r)
{
    if (!r || r->magic != C3_SETTINGS_MAGIC ||
        r->version != C3_SETTINGS_VERSION ||
        r->length != sizeof(c3_settings_t) ||
        r->commit != C3_SETTINGS_COMMIT ||
        !c3_settings_valid(&r->data)) return 0u;
    return (uint8_t)(r->crc == crc32((const uint8_t *)r,
                               (unsigned)offsetof(c3_settings_record_t, crc)));
}
uint8_t c3_settings_select_record(const c3_settings_record_t *a,
                                  const c3_settings_record_t *b)
{
    uint8_t va = c3_settings_record_valid(a), vb = c3_settings_record_valid(b);
    if (!va && !vb) return 2u;
    if (!va) return 1u;
    if (!vb) return 0u;
    return (uint8_t)(((int32_t)(a->sequence-b->sequence) >= 0) ? 0u : 1u);
}


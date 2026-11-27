/* Copyright (C) 2026 mr258876
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include "c3_settings.h"
#include "stm32f1xx_hal.h"
#include <stddef.h>
#include <string.h>

/* Keep actual FLASH access in a separate target-only file. A new record goes
 * into the OTHER page. If power fails during erase or programming, the old
 * page remains valid because commit is written LAST. */
_Static_assert(sizeof(c3_settings_record_t) == 32u, "settings record layout");
_Static_assert(offsetof(c3_settings_record_t, commit) == 28u,
               "commit halfword location");

static void read_record(uint32_t page, c3_settings_record_t *out)
{
    memcpy(out, (const void *)(uintptr_t)page, sizeof(*out));
}
uint8_t c3_settings_flash_load(c3_settings_t *out)
{
    if (!out) return 0u;
    c3_settings_record_t a,b;
    read_record(C3_SETTINGS_PAGE_A,&a);
    read_record(C3_SETTINGS_PAGE_B,&b);
    uint8_t which = c3_settings_select_record(&a,&b);
    if (which == 2u) {
        c3_settings_defaults(out);
        return 0u;
    }
    *out = which == 0u ? a.data : b.data;
    return 1u;
}
uint8_t c3_settings_flash_save(const c3_settings_t *settings)
{
    if (!c3_settings_valid(settings)) return 0u;
    c3_settings_record_t a,b,record;
    read_record(C3_SETTINGS_PAGE_A,&a);
    read_record(C3_SETTINGS_PAGE_B,&b);
    uint8_t current = c3_settings_select_record(&a,&b);
    if (current != 2u &&
        memcmp(settings, current == 0u ? &a.data : &b.data,
               sizeof(*settings)) == 0) return 1u;
    uint32_t target, next_seq;
    if (current != 2u) {
        target = (current == 0u) ? C3_SETTINGS_PAGE_B : C3_SETTINGS_PAGE_A;
        next_seq = (current == 0u ? a.sequence : b.sequence) + 1u;
    } else {
        target=C3_SETTINGS_PAGE_A;
        next_seq=1u;
    }
    c3_settings_make_record(&record, settings, next_seq);
    if (HAL_FLASH_Unlock() != HAL_OK) return 0u;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error_page = 0xFFFFFFFFu;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = target;
    erase.NbPages = 1u;
    uint8_t ok = (uint8_t)(HAL_FLASHEx_Erase(&erase,&error_page) == HAL_OK);
    const uint8_t *bytes = (const uint8_t *)&record;
    /* Program the CRC and payload first. Last a separate commit halfword;
     * reserved trailer remains erased 0xFFFF. */
    if (ok) {
        for (uint32_t off = 0u; off < offsetof(c3_settings_record_t,commit);off+=2u) {
            uint16_t hw = (uint16_t)bytes[off] | ((uint16_t)bytes[off+1u] << 8u);
            if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,target+off,hw) != HAL_OK) {
                ok = 0u; break;
            }
        }
    }
    if (ok && HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                target+offsetof(c3_settings_record_t,commit),
                C3_SETTINGS_COMMIT) != HAL_OK) ok = 0u;
    (void)HAL_FLASH_Lock();
    if (!ok) return 0u;
    c3_settings_record_t verify;
    read_record(target,&verify);
    return c3_settings_record_valid(&verify);
}

uint8_t c3_settings_flash_confirm_language(c3_settings_t *settings, uint8_t zh)
{
    if (!c3_settings_valid(settings) || zh > 1u) return 0u;
    c3_settings_t next = *settings;
    c3_settings_set_ui_prefs(&next, zh, c3_settings_startup_hint(settings));
    if (!c3_settings_flash_save(&next)) return 0u;
    *settings = next;
    return 1u;
}

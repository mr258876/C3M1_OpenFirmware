#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
O=$(mktemp -d); trap 'rm -rf "$O"' EXIT
P=C3M1_Camera/Core
F=C3M1_Camera/Middlewares/Third_Party/FatFs/src
D=(-DC3_FUNCTION_MENU=${C3_FUNCTION_MENU:-0} -DC3_FOCUS_GREEN=1 -DC3_MENU_ACTIONS=1 -DC3_I18N=1 -DC3_UI_ICONS=1 -DC3_PEAK_FULL_SP_ONLY=1 -DC3_SCORE_FULL_SP_ONLY=1 -DC3_PLAYBACK_ENABLE=1 -DC3_DNG_SOFT_PROFILE=1 -DC3_DNG_BLACK_CALIBRATION=1 -DC3_DNG_ACR_MODE=1)
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$P/Inc" "${D[@]}" tests/test_v213_nav.c "$P"/Src/{c3_ui_nav,c3_settings}.c -o "$O/nav"
"$O/nav"
# Target FatFs assumes 32-bit long; make an otherwise identical LP64 host copy.
cp "$F"/{ff.c,ff.h,integer.h,diskio.h,ff_gen_drv.h} "$O/"
sed -i 's/typedef long[[:space:]]*LONG;/typedef int LONG;/;s/typedef unsigned long[[:space:]]*DWORD;/typedef unsigned int DWORD;/' "$O/integer.h"
sed '/^#include /d' C3M1_Camera/FATFS/Target/ffconf.h > "$O/ffconf.h"
cat > "$O/stm32f1xx_hal.h" <<'H'
#include <stdint.h>
#define __IO volatile
struct dummy_gpio {uint32_t BRR,IDR,BSRR;};
extern struct dummy_gpio *GPIOB,*GPIOG;
#define GPIO_PIN_4 16u
H
cat > "$O/fatfs.h" <<'H'
#include "stm32f1xx_hal.h"
#include "ff.h"
#include "ff_gen_drv.h"
extern FATFS SDFatFS;
extern char SDPath[4];
H
# Build the actual storage wrapper and FatFs, no real disk or SD hardware.
# Unrelated photo-save sections are garbage-collected; no GPIO accesses occur.
sed -i '1i #pragma once' "$O/stm32f1xx_hal.h"
cc -std=c11 -O2 -ffunction-sections -fdata-sections -I"$O" -I"$P/Inc" "${D[@]}" tests/test_v213_format.c "$P/Src/c3_storage.c" "$O/ff.c" -Wl,--gc-sections -o "$O/format"
"$O/format"

#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
P="$HERE/C3M1_Camera"
# Keep mainline (write-capable) and diagnostic (read-only) images separate.
case "${C3_PREVIEW_PROBE:-0}" in
  0) O="$HERE/build";;
  1) case "${C3_FIFO_PROBE:-1}" in
       1) O="$HERE/build_probe";;
       0) O="$HERE/build_preview";;
       *) echo "C3_FIFO_PROBE must be 0 or 1" >&2; exit 2;;
     esac;;
  *) echo "C3_PREVIEW_PROBE must be 0 (mainline) or 1 (diagnostic)" >&2; exit 2;;
esac
# Keep all four optimization variants in distinct build directories.
if [[ -n "${C3_BUILD_TAG:-}" ]]; then
  if [[ ! "${C3_BUILD_TAG}" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "C3_BUILD_TAG must contain only A-Z, a-z, 0-9, _ or -" >&2; exit 2
  fi
  O="${O}_${C3_BUILD_TAG}"
fi
mkdir -p "$O"
I=( -I"$P/Core/Inc" -I"$P/FATFS/App" -I"$P/FATFS/Target"
    -I"$P/Middlewares/Third_Party/FatFs/src"
    -I"$P/Middlewares/Third_Party/FatFs/src/drivers"
    -I"$P/Drivers/BSP/OV7725"
    -I"$P/Drivers/SYSTEM/sys"
    -I"$P/Drivers/STM32F1xx_HAL_Driver/Inc"
    -I"$P/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy"
    -I"$P/Drivers/CMSIS/Device/ST/STM32F1xx/Include"
    -I"$P/Drivers/CMSIS/Include"
    -I"$HERE/compat")
CFLAGS=( --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -std=gnu11
         -Os -g -ffunction-sections -fdata-sections -ffreestanding -fno-builtin
         -DSTM32F103xE -DUSE_HAL_DRIVER -Wno-unused-parameter )
case "${C3_DMA_UNPACK:-0}" in
  0|1|2|3) CFLAGS+=( "-DC3_DMA_UNPACK=${C3_DMA_UNPACK:-0}" );;
  *) echo 'invalid DMA unpack mode' >&2; exit 2;;
esac
if [[ "${C3_DMA_UNPACK:-0}" != 0 && "${C3_FIFO_DMA_LIVE_ROWS:-0}" == 0 ]]; then
  echo 'DMA unpack experiments require live DMA' >&2; exit 2
fi
if [[ "${C3_DMA_UNPACK:-0}" == 3 && ( "${C3_FOCUS_GREEN:-0}" != 1 || "${C3_PEAK_EXPERIMENT:-0}" != 4 ) ]]; then
  echo 'FUSED requires GREEN FAST/SP22' >&2; exit 2
fi
for flag in C3_FOCUS_GREEN C3_LIVE_PROFILE C3_PEAK_FULL_SP_ONLY C3_SCORE_FULL_SP_ONLY; do
  value="${!flag:-0}"
  case "$value" in 0|1) CFLAGS+=( "-D$flag=$value" );; *) echo "invalid $flag" >&2; exit 2;; esac
done
case "${C3_PEAK_SP_CENTER_W:-160}" in
  160|224|256) CFLAGS+=( "-DC3_PEAK_SP_CENTER_W=${C3_PEAK_SP_CENTER_W:-160}" ) ;;
  *) echo 'invalid sparse center width' >&2; exit 2;;
esac
if [[ "${C3_FOCUS_GREEN:-0}" == 1 && "${C3_PEAK_EXPERIMENT:-0}" != 4 ]]; then
  echo 'GREEN requires FAST/SP22 mode 4' >&2; exit 2
fi
if [[ "${C3_LIVE_PROFILE:-0}" == 1 && "${C3_FIFO_DMA_LIVE_ROWS:-0}" == 0 ]]; then
  echo 'live profile requires FIFO DMA' >&2; exit 2
fi
case "${C3_FIFO_DMA_LIVE_ROWS:-0}" in
  0|1|4) if [[ "${C3_FIFO_DMA_LIVE_ROWS:-0}" != 0 ]]; then
    CFLAGS+=( "-DC3_FIFO_DMA_LIVE_ROWS=${C3_FIFO_DMA_LIVE_ROWS}" "-DC3_FIFO_DMA_LIVE_TICKS=${C3_FIFO_DMA_LIVE_TICKS:-14}" );
  fi;;
  *) echo "invalid live DMA block size" >&2; exit 2;;
esac
case "${C3_FIFO_DMA_LOAD:-0}" in
  0|1) CFLAGS+=( "-DC3_FIFO_DMA_LOAD=${C3_FIFO_DMA_LOAD:-0}" );;
  *) echo "C3_FIFO_DMA_LOAD must be 0 or 1" >&2; exit 2;;
esac
case "${C3_FIFO_DMA_HIGH:-0}" in
  0|1) CFLAGS+=( "-DC3_FIFO_DMA_HIGH=${C3_FIFO_DMA_HIGH:-0}" );;
  *) echo "C3_FIFO_DMA_HIGH must be 0 or 1" >&2; exit 2;;
esac
case "${C3_FIFO_DMA_MARGIN:-0}" in
  0|1) CFLAGS+=( "-DC3_FIFO_DMA_MARGIN=${C3_FIFO_DMA_MARGIN:-0}" );;
  *) echo "C3_FIFO_DMA_MARGIN must be 0 or 1" >&2; exit 2;;
esac
case "${C3_FIFO_DMA_PROBE:-0}" in
  0|1) CFLAGS+=( "-DC3_FIFO_DMA_PROBE=${C3_FIFO_DMA_PROBE:-0}" );;
  *) echo "C3_FIFO_DMA_PROBE must be 0 or 1" >&2; exit 2;;
esac
case "${C3_PEAK_EXPERIMENT:-0}" in
  0|1|2|3|4) CFLAGS+=( "-DC3_PEAK_EXPERIMENT=${C3_PEAK_EXPERIMENT:-0}" ) ;;
  *) echo 'C3_PEAK_EXPERIMENT must be 0, 1, 2, 3 or 4' >&2; exit 2;;
esac
case "${C3_PEAK_PROFILE:-0}" in
  0|1) CFLAGS+=( "-DC3_PEAK_PROFILE=${C3_PEAK_PROFILE:-0}" ) ;;
  *) echo 'C3_PEAK_PROFILE must be 0 or 1' >&2; exit 2;;
esac
if [[ "${C3_PEAK_PROFILE:-0}" == 1 && ( "${C3_PREVIEW_PROFILE:-0}" != 1 || "${C3_LCD_DMA_ENABLE:-1}" != 1 || "${C3_PEAK_EXPERIMENT:-0}" == 0 ) ]]; then
  echo 'peak profile requires experiment, DWT and LCD DMA' >&2; exit 2
fi
case "${C3_ZOOM_AE_LOCK:-0}" in
  0|1) CFLAGS+=( "-DC3_ZOOM_AE_LOCK=${C3_ZOOM_AE_LOCK:-0}" ) ;;
  *) echo "invalid C3_ZOOM_AE_LOCK" >&2; exit 2;;
esac
case "${C3_METER_DIAG:-0}" in
  0|1) CFLAGS+=( "-DC3_METER_DIAG=${C3_METER_DIAG:-0}" ) ;;
  *) echo "C3_METER_DIAG must be 0 or 1" >&2; exit 2;;
esac
# v0.1.44 mainline: camera -O3 + Full LTO field-tested for ~29.8 FPS
# on the read-only D3 image. Other TUs remain -Os; link-time -O2.
# Override C3_CAMERA_OPT_LEVEL=2 C3_LTO=0 for the previous build profile.
# Full mainline (capture/SD) requires separate hardware validation.
case "${C3_CAMERA_OPT_LEVEL:-3}" in
  s|2|3) : ;;
  *) echo "C3_CAMERA_OPT_LEVEL must be s, 2 or 3" >&2; exit 2;;
esac
case "${C3_LTO:-1}" in
  0) : ;;
  1) CFLAGS+=( -flto=full ) ;;
  *) echo "C3_LTO must be 0 or 1" >&2; exit 2;;
esac
# v0.1.44: promote field-tested D3 staged peaking into the mainline.
# 1 = prior official E1 grid only; 3 = full-resolution staged peaking.
# Still capture, SCCB, GPIO, LCD DMA and FSMC are unchanged.
case "${C3_OVERLAY_OPT:-3}" in
  0|1|2|3) CFLAGS+=( "-DC3_OVERLAY_OPT=${C3_OVERLAY_OPT:-3}" ) ;;
  *) echo "C3_OVERLAY_OPT must be 0, 1, 2 or 3" >&2; exit 2;;
esac
if [[ "${C3_OVERLAY_OPT:-3}" != "0" && ( "${C3_PREVIEW_ROW_OPT:-2}" != "2" || "${C3_FIFO_ASM_MODE:-0}" != "0" ) ]]; then
  echo "C3_OVERLAY_OPT requires RowOpt2 and FIFO ASM off" >&2; exit 2
fi
# v0.1.44: peaking-only CPU experiment; 0 is the exact D3 math/bytes.
# 1 = memcpy/REV16 RGB565 pixel accesses, 2 = plus gradient simplification.
case "${C3_PEAK_OPT:-0}" in
  0|1|2) CFLAGS+=( "-DC3_PEAK_OPT=${C3_PEAK_OPT:-0}" ) ;;
  *) echo "C3_PEAK_OPT must be 0, 1 or 2" >&2; exit 2;;
esac
if [[ "${C3_PEAK_OPT:-0}" != "0" && "${C3_OVERLAY_OPT:-3}" != "3" ]]; then
  echo "C3_PEAK_OPT requires staged peaking (C3_OVERLAY_OPT=3)" >&2; exit 2
fi
# v0.1.57: centre-only 160x120 peaking, FULL remains selectable as a
# build-only exact fallback. Does not change focus-score ROI or the preview
# FIFO/LCD data path. Default 1 applies to mainline AND read-only preview.
case "${C3_PEAK_CENTER_ONLY:-1}" in
  0|1) CFLAGS+=( "-DC3_PEAK_CENTER_ONLY=${C3_PEAK_CENTER_ONLY:-1}" ) ;;
  *) echo "C3_PEAK_CENTER_ONLY must be 0 or 1" >&2; exit 2 ;;
esac
# Dedicated READ-ONLY overlay A/B test: 4X=GRID, ISO=PEAK.
case "${C3_OVERLAY_DIAG:-0}" in
  0|1) CFLAGS+=( "-DC3_OVERLAY_DIAG=${C3_OVERLAY_DIAG:-0}" ) ;;
  *) echo "C3_OVERLAY_DIAG must be 0 or 1" >&2; exit 2;;
esac
if [[ "${C3_OVERLAY_DIAG:-0}" == "1" && ( "${C3_PREVIEW_PROBE:-0}" != "1" || "${C3_FIFO_PROBE:-1}" != "0" ) ]]; then
  echo "C3_OVERLAY_DIAG requires read-only live preview" >&2; exit 2
fi
# v0.1.40: optional Thumb-2 FIFO preview-only instruction experiments.
# 0=field-tested O2 baseline; 1=asm byte triplet (plain/OSD); 2=asm plain
# row loop and original O2 OSD path. These never change still capture reads.
case "${C3_FIFO_ASM_MODE:-0}" in
  0|1|2) if [[ "${C3_FIFO_ASM_MODE:-0}" != "0" ]]; then CFLAGS+=( "-DC3_FIFO_ASM_MODE=${C3_FIFO_ASM_MODE}" ); fi ;;
  *) echo "C3_FIFO_ASM_MODE must be 0, 1, or 2" >&2; exit 2;;
esac
if [[ "${C3_FIFO_ASM_MODE:-0}" != "0" && "${C3_PREVIEW_ROW_OPT:-2}" != "2" ]]; then
  echo "C3_FIFO_ASM_MODE requires RowOpt2" >&2; exit 2
fi
if [[ "${C3_FIFO_ASM_MODE:-0}" != "0" && "${C3_FIFO_CPU_BURST_BYTES:-0}" != "0" ]]; then
  echo "C3_FIFO_ASM_MODE requires FIFO B0" >&2; exit 2
fi
# v0.1.39: preview RowOpt2 only; RCLK and FIFO byte reader unchanged.
case "${C3_ROW_PACK16:-0}" in
  0|1) CFLAGS+=( "-DC3_ROW_PACK16=${C3_ROW_PACK16:-0}" ) ;;
  *) echo "C3_ROW_PACK16 must be 0 or 1" >&2; exit 2;;
esac
case "${C3_FIFO_ROW_ARM:-0}" in
  0|1|2) CFLAGS+=( "-DC3_FIFO_ROW_ARM=${C3_FIFO_ROW_ARM:-0}" ) ;;
  *) echo "C3_FIFO_ROW_ARM must be 0, 1 or 2" >&2; exit 2;;
esac
if [[ "${C3_FIFO_ROW_ARM:-0}" != 0 && ( "${C3_ROW_PACK16:-0}" != 0 || "${C3_FIFO_ASM_MODE:-0}" != 0 || "${C3_FIFO_CPU_BURST_BYTES:-0}" != 0 || "${C3_PREVIEW_ROW_OPT:-2}" != 2 || "${C3_LCD_DMA_ENABLE:-1}" != 1 ) ]]; then
  echo 'low-register FIFO reader requires RowOpt2 LCD DMA and no other FIFO experiments' >&2; exit 2
fi
if [[ "${C3_PREVIEW_PIPELINE:-1}" == "0" ]]; then
  CFLAGS+=( -DC3_PREVIEW_PIPELINE=0 )
fi
# v0.1.38: use the measured stable DATAST=4 by default (D4).
# Explicit DATAST=8 restores the original slower LCD timing.
case "${C3_FSMC_DATAST:-4}" in
  8|6|5|4) : ;;
  *) echo "C3_FSMC_DATAST must be 8, 6, 5 or 4" >&2; exit 2;;
esac
# With the variable unset, compile fsmc.c's new default DATAST=4.
if [[ -n "${C3_FSMC_DATAST:-}" ]]; then
  CFLAGS+=( "-DC3_FSMC_DATAST=${C3_FSMC_DATAST}" )
fi
# v0.1.38 default is the tested RowOpt2; opt=0 is a clean rollback.
case "${C3_PREVIEW_ROW_OPT:-2}" in
  0|1|2) CFLAGS+=( "-DC3_PREVIEW_ROW_OPT=${C3_PREVIEW_ROW_OPT:-2}" );;
  *) echo "C3_PREVIEW_ROW_OPT must be 0, 1 or 2" >&2; exit 2;;
esac
# v0.1.34 conservative CPU read-burst experiment, no timer or FIFO DMA.
# This changes the copy-loop structure only for the normal (no effect) LCD DMA preview row.
case "${C3_FIFO_CPU_BURST_BYTES:-0}" in
  0|4|8) CFLAGS+=( "-DC3_FIFO_CPU_BURST_BYTES=${C3_FIFO_CPU_BURST_BYTES:-0}" );;
  *) echo "C3_FIFO_CPU_BURST_BYTES must be 0, 4, or 8" >&2; exit 2;;
esac
# Field-tested LCD DMA mainline default, explicit 0 restores CPU write path.
case "${C3_LCD_DMA_ENABLE:-1}" in
  0|1) CFLAGS+=( "-DC3_LCD_DMA_ENABLE=${C3_LCD_DMA_ENABLE:-1}" );;
  *) echo "C3_LCD_DMA_ENABLE must be 0 or 1" >&2; exit 2;;
esac
if [[ "${C3_PREVIEW_PROFILE:-0}" == "1" ]]; then
  CFLAGS+=( -DC3_PREVIEW_PROFILE=1 )
fi
# v0.1.36: read-only stage profiling, no camera/FIFO/RCLK change.
case "${C3_STAGE_PROFILE:-0}" in
  0|1) CFLAGS+=( "-DC3_STAGE_PROFILE=${C3_STAGE_PROFILE:-0}" );;
  *) echo "C3_STAGE_PROFILE must be 0 or 1" >&2; exit 2;;
esac
if [[ "${C3_STAGE_PROFILE:-0}" == "1" ]]; then
  if [[ "${C3_PREVIEW_PROFILE:-0}" != "1" || "${C3_PREVIEW_PROBE:-0}" != "1" || "${C3_FIFO_PROBE:-1}" != "0" || "${C3_LCD_DMA_ENABLE:-1}" != "1" || "${C3_PROFILE_HUD:-0}" != "1" ]]; then
    echo "C3_STAGE_PROFILE requires read-only live preview + DWT + LCD DMA + HUD" >&2
    exit 2
  fi
fi
# v0.1.38: 0=off, 1=direct OSD vs non-OSD rows,
# 2=buffered OSD FIFO fetch and LCD composition times.
case "${C3_ROW_DETAIL_PROFILE:-0}" in
  0|1|2) CFLAGS+=( "-DC3_ROW_DETAIL_PROFILE=${C3_ROW_DETAIL_PROFILE:-0}" );;
  *) echo "C3_ROW_DETAIL_PROFILE must be 0, 1, or 2" >&2; exit 2;;
esac
if [[ "${C3_ROW_DETAIL_PROFILE:-0}" != "0" ]]; then
  if [[ "${C3_STAGE_PROFILE:-0}" != "1" || "${C3_PREVIEW_ROW_OPT:-2}" != "2" || "${C3_FIFO_CPU_BURST_BYTES:-0}" != "0" || "${C3_PREVIEW_PIPELINE:-1}" != "1" ]]; then
    echo "C3_ROW_DETAIL_PROFILE requires StageHUD with RowOpt2, FIFO B0, and preview pipeline" >&2
    exit 2
  fi
fi
# Performance HUD only in read-only profile firmware, never as a mainline default.
case "${C3_PROFILE_HUD:-0}" in
  0|1) CFLAGS+=( "-DC3_PROFILE_HUD=${C3_PROFILE_HUD:-0}" );;
  *) echo "C3_PROFILE_HUD must be 0 or 1" >&2; exit 2;;
esac
for flag in C3_BOOT_OVERLAP C3_BOOT_DIAG C3_PLAYBACK_ENABLE C3_ZOOM_LOCK_DIAG C3_I18N C3_UI_ICONS C3_MENU_ACTIONS C3_FUNCTION_MENU C3_STARTUP_HINT C3_LANGUAGE_SETUP C3_LANGUAGE_LAYOUT C3_DEFAULTS_V218 C3_POSTSHOT C3_POSTSHOT_IMMEDIATE C3_POSTSHOT_KEEP_BG; do
  value="${!flag:-0}"
  case "$value" in 0|1) CFLAGS+=( "-D$flag=$value" );; *) echo "invalid $flag" >&2; exit 2;; esac
done
# Optional override for controlled cold-boot comparison (default 1000ms).
if [[ -n "${C3_LCD_POWER_SETTLE_MS:-}" ]]; then
  if [[ ! "$C3_LCD_POWER_SETTLE_MS" =~ ^[0-9]+$ ]] || (( 10#$C3_LCD_POWER_SETTLE_MS > 30000 )); then
    echo "C3_LCD_POWER_SETTLE_MS must be an integer from 0 to 30000" >&2
    exit 2
  fi
  CFLAGS+=( "-DC3_LCD_POWER_SETTLE_MS=${C3_LCD_POWER_SETTLE_MS}u" )
fi
# CMOS minimum warmup measured from GPIO release (parallel mode).
if [[ -n "${C3_CAM_POWER_SETTLE_MS:-}" ]]; then
  if [[ ! "$C3_CAM_POWER_SETTLE_MS" =~ ^[0-9]+$ ]] || (( 10#$C3_CAM_POWER_SETTLE_MS > 30000 )); then
    echo "C3_CAM_POWER_SETTLE_MS must be an integer from 0 to 30000" >&2; exit 2
  fi
  CFLAGS+=( "-DC3_CAM_POWER_SETTLE_MS=${C3_CAM_POWER_SETTLE_MS}u" )
fi
# SCCB timing A/B without editing the hardware GPIO setup.
case "${C3_SCCB_MAIN_HALF_US:-50}" in
  20|50|100) CFLAGS+=( "-DC3_SCCB_MAIN_HALF_US=${C3_SCCB_MAIN_HALF_US:-50}u" );;
  *) echo "C3_SCCB_MAIN_HALF_US must be 20 or 50" >&2; exit 2;;
esac
# Experimental RGB readback policy: 1 = warn for 0xFF readback only;
# 0 restores v0.1.25 strict RGB init. RAW/DNG is strict by default.
case "${C3_RGB_VERIFY_WARN_ONLY:-1}" in
  0|1) CFLAGS+=( "-DC3_RGB_VERIFY_WARN_ONLY=${C3_RGB_VERIFY_WARN_ONLY:-1}" );;
  *) echo "C3_RGB_VERIFY_WARN_ONLY must be 0 or 1" >&2; exit 2;;
esac
# v0.1.50: verified workaround: release PG13 and switch to floating
# INPUT for the eight SCCB slave data bits; restore OD for ninth/NACK/STOP.
# Explicit 0 recompiles the legacy OD receiver byte-for-byte.
case "${C3_SCCB_FLOAT_READ:-1}" in
  0|1) CFLAGS+=( "-DC3_SCCB_FLOAT_READ=${C3_SCCB_FLOAT_READ:-1}" ) ;;
  *) echo "C3_SCCB_FLOAT_READ must be 0 or 1" >&2; exit 2;;
esac
# v0.1.45: on a readback mismatch, try repeated-START (strict compare still required).
case "${C3_SCCB_RESTART_VERIFY:-1}" in
  0|1) CFLAGS+=( "-DC3_SCCB_RESTART_VERIFY=${C3_SCCB_RESTART_VERIFY:-1}" ) ;;
  *) echo "C3_SCCB_RESTART_VERIFY must be 0 or 1" >&2; exit 2;;
esac
# Read-only on-camera matrix: RGB/RAW and 20/50us with legacy / restart reads.
case "${C3_SCCB_SCAN_DIAG:-0}" in
  0|1) :;;
  *) echo "C3_SCCB_SCAN_DIAG must be 0 or 1" >&2; exit 2;;
esac
if [[ "${C3_SCCB_SCAN_DIAG:-0}" == "1" ]]; then
  if [[ "${C3_PREVIEW_PROBE:-0}" != "1" || "${C3_FIFO_PROBE:-1}" != "0" ]]; then
    echo "C3_SCCB_SCAN_DIAG requires C3_PREVIEW_PROBE=1 C3_FIFO_PROBE=0" >&2
    exit 2
  fi
  CFLAGS+=( -DC3_SCCB_SCAN_DIAG=1 )
fi
# DNG/RAW FF-only diagnostic: DEFAULT=0 (strict), optional=1 (experimental).
# Never use the optional build on a card containing important data.
case "${C3_RAW_VERIFY_WARN_IF_FF:-0}" in
  0|1) CFLAGS+=( "-DC3_RAW_VERIFY_WARN_IF_FF=${C3_RAW_VERIFY_WARN_IF_FF:-0}" );;
  *) echo "C3_RAW_VERIFY_WARN_IF_FF must be 0 or 1" >&2; exit 2;;
esac
# v28 mirror correction: enable horizontal mirror relative to 0x90/0xB0.
# 0xD0 = no sensor B/R swap, 0xF0 = sensor B/R swap.
case "${C3_CAM_COM3_RGB:-0xD0}" in
  0xD0|0xF0) CFLAGS+=( "-DC3_CAM_COM3_RGB=${C3_CAM_COM3_RGB:-0xD0}u" );;
  *) echo "C3_CAM_COM3_RGB must be 0xD0 or 0xF0" >&2; exit 2;;
esac
for v in C3_LCD_DEFAULT_BGR C3_LCD_DEFAULT_INV_ON C3_LCD_COLOR_DIAG; do
  val="${!v:-}"
  case "${val:-0}" in
    0|1) :;;
    *) echo "$v must be 0 or 1" >&2; exit 2;;
  esac
  if [[ -n "$val" ]]; then CFLAGS+=( "-D$v=$val" ); fi
done
case "${C3_CAM_COLOR_BAR:-0}" in
  0|1) CFLAGS+=( "-DC3_CAM_COLOR_BAR=${C3_CAM_COLOR_BAR:-0}" );;
  *) echo "C3_CAM_COLOR_BAR must be 0 or 1" >&2; exit 2;;
esac
# v0.1.65: dynamic AWB, tint-polarity correction, 67.5% experimental DNG
# profile, and D50 declaration (separately switchable for A/B or rollback).
for feature in C3_DNG_DYNAMIC_WB C3_DNG_EXPERIMENTAL_MATRIX C3_DNG_TINT_REVERSE C3_DNG_CALIBRATION_D50; do
  val="${!feature:-1}"
  case "$val" in
    0|1) CFLAGS+=( "-D${feature}=${val}" );;
    *) echo "$feature must be 0 or 1" >&2; exit 2;;
  esac
done
# v0.1.73: embed the Camera Raw-tested 67.5% Soft DNG profile.
# Feature off is byte-exact v0.1.72 compatibility.
case "${C3_DNG_SOFT_PROFILE:-0}" in
  0|1) CFLAGS+=( "-DC3_DNG_SOFT_PROFILE=${C3_DNG_SOFT_PROFILE:-0}" ) ;;
  *) echo 'C3_DNG_SOFT_PROFILE must be 0 or 1' >&2; exit 2 ;;
esac
if [[ "${C3_DNG_SOFT_PROFILE:-0}" == 1 && "${C3_DNG_EXPERIMENTAL_MATRIX:-1}" != 1 ]]; then
  echo 'C3_DNG_SOFT_PROFILE requires C3_DNG_EXPERIMENTAL_MATRIX=1' >&2; exit 2
fi
# v179 black calibration is specific to BLC ON / v177 RAW timing / Soft.
case "${C3_DNG_BLACK_CALIBRATION:-0}" in
  0|1) CFLAGS+=( "-DC3_DNG_BLACK_CALIBRATION=${C3_DNG_BLACK_CALIBRATION:-0}" ) ;;
  *) echo 'C3_DNG_BLACK_CALIBRATION must be 0 or 1' >&2; exit 2 ;;
esac
case "${C3_DNG_ACR_MODE:-0}" in
  0|1|2) CFLAGS+=( "-DC3_DNG_ACR_MODE=${C3_DNG_ACR_MODE:-0}" ) ;;
  *) echo 'C3_DNG_ACR_MODE must be 0, 1 or 2' >&2; exit 2 ;;
esac
if [[ "${C3_DNG_BLACK_CALIBRATION:-0}" == 1 &&
      ( "${C3_DNG_SOFT_PROFILE:-0}" != 1 || "${C3_SHUTTER_DYNAMIC_TIMING:-0}" != 1 ||
        "${C3_RAW_BLC_ENABLE:-1}" != 1 || "${C3_RAW_CLKRC:-0}" != 0 ||
        "${C3_DNG_SETTLE_LOCKED:-2}" != 6 || "${C3_RAW_FREEZE_AWB:-1}" != 1 ) ]]; then
  echo 'v179 calibration requires Soft, BLC ON, v177 RAW timing, 6 settling frames, frozen AWB' >&2; exit 2
fi
# v0.1.55: field-tested T4 sensor clock is now the RGB565 default.
# 1: write COM4=0x41 after the RGB init table (same order as v0.1.54 T4).
# 0: exact v0.1.54 8x full-mainline rollback. RAW table is unaffected.
case "${C3_RGB_PLL4_DEFAULT:-1}" in
  0|1) CFLAGS+=( "-DC3_RGB_PLL4_DEFAULT=${C3_RGB_PLL4_DEFAULT:-1}" ) ;;
  *) echo "C3_RGB_PLL4_DEFAULT must be 0 or 1" >&2; exit 2 ;;
esac
# v0.1.52: isolated read-only colorbar PCLK/WCK timing A/B.
for flag in C3_TEST_PCLK_INV C3_TEST_PLL4; do
  value="${!flag:-0}"
  case "$value" in
    0|1) CFLAGS+=( "-D${flag}=$value" );;
    *) echo "$flag must be 0 or 1" >&2; exit 2;;
  esac
  if [[ "$value" == 1 && ( "${C3_PREVIEW_PROBE:-0}" != 1 || "${C3_FIFO_PROBE:-1}" != 0 ) ]]; then
    echo "$flag requires read-only live or colorbar probe" >&2; exit 2
  fi
  if [[ "$value" == 1 && "$flag" == C3_TEST_PCLK_INV && "${C3_CAM_COLOR_BAR:-0}" != 1 ]]; then
    echo "PCLK inversion remains restricted to sensor colorbar" >&2; exit 2
  fi
done
# v0.1.53: sensor clock comparison — all are read-only colorbar builds.
for flag in C3_TEST_PLL6 C3_TEST_CLKDIV2; do
  value="${!flag:-0}"
  case "$value" in
    0|1) CFLAGS+=( "-D${flag}=$value" ) ;;
    *) echo "$flag must be 0 or 1" >&2; exit 2 ;;
  esac
  if [[ "$value" == 1 && ( "${C3_PREVIEW_PROBE:-0}" != 1 || "${C3_FIFO_PROBE:-1}" != 0 ) ]]; then
    echo "$flag requires read-only live or colorbar probe" >&2; exit 2
  fi
  if [[ "$value" == 1 && "$flag" == C3_TEST_PLL6 && "${C3_CAM_COLOR_BAR:-0}" != 1 ]]; then
    echo "PLL6 remains restricted to sensor colorbar" >&2; exit 2
  fi
done
if (( ${C3_TEST_PLL6:-0} + ${C3_TEST_CLKDIV2:-0} + ${C3_TEST_PLL4:-0} + ${C3_TEST_PCLK_INV:-0} > 1 )); then
  echo "Only one sensor clock diagnostic may be selected at a time" >&2; exit 2
fi
case "${C3_COLOR_SWATCH_DIAG:-0}" in
  0|1) CFLAGS+=( "-DC3_COLOR_SWATCH_DIAG=${C3_COLOR_SWATCH_DIAG:-0}" );;
  *) echo "C3_COLOR_SWATCH_DIAG must be 0 or 1" >&2; exit 2;;
esac
# Mainline: enable normal preview, BMP/DNG, SD and settings-Flash code.
# Diagnostic: choose FIFO A/B (default) or preview-only diagnostic loop.
if [[ "${C3_PREVIEW_PROBE:-0}" == "1" ]]; then
  case "${C3_FIFO_PROBE:-1}" in
    0|1) CFLAGS+=( "-DC3_FIFO_PROBE=${C3_FIFO_PROBE:-1}" );;
    *) echo "C3_FIFO_PROBE must be 0 or 1" >&2; exit 2;;
  esac
  CFLAGS+=( -DC3_PREVIEW_PROBE=1 )
else
  CFLAGS+=( -DC3_PREVIEW_PROBE=0 -DC3_FIFO_PROBE=0 )
fi
# v0.1.62: individually controlled still-capture experiments.
# v168: user option, no completion banner or fixed pause in the normal build.
case "${C3_NO_SAVED:-1}" in
  0|1) CFLAGS+=( "-DC3_NO_SAVED=${C3_NO_SAVED:-1}" ) ;;
  *) echo 'C3_NO_SAVED must be 0 or 1' >&2; exit 2 ;;
esac
# v170 mainline: SDIO DMA2 Channel 4 is the default; explicit 0 restores polling.
# Hardware performance must still be measured with a known-good SD card.
case "${C3_SDIO_TX_DMA:-1}" in
  0|1) CFLAGS+=( "-DC3_SDIO_TX_DMA=${C3_SDIO_TX_DMA:-1}" ) ;;
  *) echo 'C3_SDIO_TX_DMA must be 0 or 1' >&2; exit 2 ;;
esac
# v172: RAW uses the RGB preview AWB snapshot, not a second RAW AWB pass.
case "${C3_RAW_FREEZE_AWB:-1}" in
  0|1) CFLAGS+=( "-DC3_RAW_FREEZE_AWB=${C3_RAW_FREEZE_AWB:-1}" ) ;;
  *) echo 'C3_RAW_FREEZE_AWB must be 0 or 1' >&2; exit 2 ;;
esac
# v172: display estimated exposure seconds instead of AEC register rows.
case "${C3_OSD_SHUTTER_TIME:-1}" in
  0|1) CFLAGS+=( "-DC3_OSD_SHUTTER_TIME=${C3_OSD_SHUTTER_TIME:-1}" ) ;;
  *) echo 'C3_OSD_SHUTTER_TIME must be 0 or 1' >&2; exit 2 ;;
esac
# v166: reduce the post-save UI-only fixed delay from 230ms to 100ms.
# The bitmap/raw data are fully saved BEFORE this UI confirmation.
case "${C3_SAVED_HOLD_MS:-100}" in
  0|50|80|100|150|230) CFLAGS+=( "-DC3_SAVED_HOLD_MS=${C3_SAVED_HOLD_MS:-100}" ) ;;
  *) echo 'C3_SAVED_HOLD_MS must be 0/50/80/100/150/230' >&2; exit 2 ;;
esac
# v166: nonreset preview exposure updates by default; experimental RAW
# RGB565->RAW8 differential switch is an explicit A/B opt-in.
for spec in 'C3_FAST_PREVIEW_CONTROL:1' 'C3_FAST_RAW_SWITCH:0'; do
  name="${spec%%:*}"; def="${spec#*:}"; val="${!name:-$def}"
  case "$val" in 0|1) CFLAGS+=( "-D${name}=${val}" ) ;;
    *) echo "${name} must be 0 or 1" >&2; exit 2 ;;
  esac
done
case "${C3_FAST_RGB_RESTORE:-0}" in
  0|1) CFLAGS+=( "-DC3_FAST_RGB_RESTORE=${C3_FAST_RGB_RESTORE:-0}" ) ;;
  *) echo 'C3_FAST_RGB_RESTORE must be 0 or 1' >&2; exit 2 ;;
esac
if [[ "${C3_FAST_RGB_RESTORE:-0}" == "1" && "${C3_FAST_RAW_SWITCH:-0}" != "1" ]]; then
  echo 'C3_FAST_RGB_RESTORE requires C3_FAST_RAW_SWITCH=1' >&2; exit 2
fi
if [[ "${C3_FAST_RGB_RESTORE:-0}" == "1" && "${C3_RAW_LOCK_EXPOSURE:-1}" != "1" ]]; then
  echo 'C3_FAST_RGB_RESTORE requires C3_RAW_LOCK_EXPOSURE=1' >&2; exit 2
fi
if [[ "${C3_FAST_RAW_SWITCH:-0}" == "1" ]]; then
  if [[ "${C3_PREVIEW_PROBE:-0}" != "0" || "${C3_RAW_CLKRC:-0}" != "0" ||
        "${C3_RGB_PLL4_DEFAULT:-1}" != "1" ]]; then
    echo 'C3_FAST_RAW_SWITCH requires mainline, RAW CLKRC0 and RGB PLL4' >&2
    exit 2
  fi
fi
for spec in 'C3_FAST_BMP_ENABLE:1' 'C3_RAW_LOCK_EXPOSURE:1' 'C3_RAW_CLKRC:0' 'C3_RAW_BLC_ENABLE:1'; do
  flag="${spec%%:*}"; fallback="${spec#*:}"; value="${!flag:-$fallback}"
  case "$value" in 0|1) CFLAGS+=( "-D${flag}=${value}" );;
    *) echo "$flag must be 0 or 1" >&2; exit 2;; esac
done
case "${C3_DNG_SETTLE_LOCKED:-2}" in
  1|2|3|4|5|6) CFLAGS+=( "-DC3_DNG_SETTLE_LOCKED=${C3_DNG_SETTLE_LOCKED:-2}" );;
  *) echo 'C3_DNG_SETTLE_LOCKED must be 1..6' >&2; exit 2;;
esac
# v180: explicit VSYNC-only settling opt-in; old wrappers retain legacy.
case "${C3_DNG_VSYNC_SETTLE:-0}" in
  0|1) CFLAGS+=( "-DC3_DNG_VSYNC_SETTLE=${C3_DNG_VSYNC_SETTLE:-0}" ) ;;
  *) echo 'C3_DNG_VSYNC_SETTLE must be 0 or 1' >&2; exit 2 ;;
esac
case "${C3_DNG_FAST_SETTLE_FRAMES:-6}" in
  1|2|3|6) CFLAGS+=( "-DC3_DNG_FAST_SETTLE_FRAMES=${C3_DNG_FAST_SETTLE_FRAMES:-6}" ) ;;
  *) echo 'C3_DNG_FAST_SETTLE_FRAMES must be 1, 2, 3 or 6' >&2; exit 2 ;;
esac
if [[ "${C3_DNG_VSYNC_SETTLE:-0}" == 1 && "${C3_DNG_SETTLE_LOCKED:-2}" != 6 ]]; then
  echo 'VSYNC settling requires six-frame conservative fallback' >&2; exit 2
fi
if [[ "${C3_DNG_VSYNC_SETTLE:-0}" == 0 && "${C3_DNG_FAST_SETTLE_FRAMES:-6}" != 6 ]]; then
  echo 'Short waits require explicit VSYNC settling opt-in' >&2; exit 2
fi
# RGB565 initialization starts at the tested 0x80; dynamic mainline
# subsequently selects 0x8A/0xAE/0x0D8/0x2CC as needed.
# Set C3_SHUTTER_DYNAMIC_TIMING=0 to build with the original constant EXHCL.
case "${C3_RGB_EXHCL:-0x80}" in
  0x9E|0x80|0x40|0x00)
    CFLAGS+=( "-DC3_RGB_EXHCL=${C3_RGB_EXHCL:-0x80}u" ) ;;
  *) echo 'C3_RGB_EXHCL must be 0x9E, 0x80, 0x40 or 0x00' >&2; exit 2 ;;
esac
# v177 mainline: dynamic EXHCH/EXHCL/CLKRC shutter plans (1X, 4X, RAW8).
case "${C3_SHUTTER_DYNAMIC_TIMING:-1}" in
  0|1) CFLAGS+=( "-DC3_SHUTTER_DYNAMIC_TIMING=${C3_SHUTTER_DYNAMIC_TIMING:-1}" ) ;;
  *) echo 'C3_SHUTTER_DYNAMIC_TIMING must be 0 or 1' >&2; exit 2 ;;
esac
case "${C3_4X_FAST_8000:-1}" in
  0|1) CFLAGS+=( "-DC3_4X_FAST_8000=${C3_4X_FAST_8000:-1}" ) ;;
  *) echo 'C3_4X_FAST_8000 must be 0 or 1' >&2; exit 2 ;;
esac
if [[ "${C3_SHUTTER_DYNAMIC_TIMING:-1}" == 1 &&
      ( "${C3_RGB_EXHCL:-0x80}" != 0x80 || "${C3_RAW_CLKRC:-0}" != 0 ||
        "${C3_SENSOR_PCLK_HZ:-12000000}" != 12000000 ||
        "${C3_SHUTTER_DATASHEET_TIMING:-1}" != 1 ) ]]; then
  echo 'v177 dynamic plan requires EXHCL80 RAWCLK0 PCLK12M datasheet model' >&2; exit 2
fi
# v0.1.69: datasheet-based shutter row calculations. 0 is byte-identical
# v0.1.68 exposure table / RAW CLKRC-only conversion fallback.
case "${C3_SHUTTER_DATASHEET_TIMING:-1}" in
  0|1) CFLAGS+=( "-DC3_SHUTTER_DATASHEET_TIMING=${C3_SHUTTER_DATASHEET_TIMING:-1}" ) ;;
  *) echo 'C3_SHUTTER_DATASHEET_TIMING must be 0 or 1' >&2; exit 2 ;;
esac
case "${C3_SENSOR_PCLK_HZ:-12000000}" in
  6000000|8000000|10000000|12000000|16000000|24000000)
    CFLAGS+=( "-DC3_SENSOR_PCLK_HZ=${C3_SENSOR_PCLK_HZ:-12000000}u" ) ;;
  *) echo 'C3_SENSOR_PCLK_HZ must be 6000000/8000000/10000000/12000000/16000000/24000000' >&2; exit 2 ;;
esac
FILES=(
 Core/Src/main.c Core/Src/c3_i18n.c Core/Src/gpio.c Core/Src/fsmc.c Core/Src/sdio.c
 Core/Src/stm32f1xx_it.c Core/Src/system_stm32f1xx.c
 Core/Src/c3_camera.c Core/Src/c3_sd_dma_write.c Core/Src/c3_sleep.c Core/Src/c3_battery.c Core/Src/c3_lcd_dma.c Core/Src/c3_fifo_probe.c Core/Src/c3_fifo_dma_probe.c Core/Src/c3_zoom.c Core/Src/c3_frame_sched.c Core/Src/c3_lcd.c Core/Src/c3_font.c Core/Src/c3_osd.c Core/Src/c3_focus.c Core/Src/c3_fps.c Core/Src/c3_buttons.c Core/Src/c3_input.c Core/Src/c3_exposure.c Core/Src/c3_meter.c Core/Src/c3_ui_nav.c Core/Src/c3_settings.c Core/Src/c3_settings_flash.c Core/Src/c3_menu.c Core/Src/c3_capture_recovery.c Core/Src/c3_storage.c Core/Src/c3_dng.c Core/Src/c3_bmp.c Core/Src/c3_play_decode.c Core/Src/c3_playback.c
 Drivers/SYSTEM/delay/delay.c Drivers/BSP/EXTI/exti.c
 Drivers/BSP/OV7725/sccb.c Drivers/BSP/OV7725/ov7725.c
 FATFS/App/fatfs.c FATFS/Target/bsp_driver_sd.c
 Middlewares/Third_Party/FatFs/src/ff.c
 Middlewares/Third_Party/FatFs/src/diskio.c
 Middlewares/Third_Party/FatFs/src/ff_gen_drv.c
 Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sram.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_fsmc.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_sd.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_ll_sdmmc.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c
 Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c
)
if [[ "${C3_SHUTTER_DYNAMIC_TIMING:-1}" == 1 ]]; then
  FILES+=( Core/Src/c3_shutter_timing.c )
fi
# Compile the diagnostic UI only in the explicitly read-only firmware.
if [[ "${C3_SCCB_SCAN_DIAG:-0}" == "1" ]]; then
  FILES+=( Core/Src/c3_sccb_diag.c )
fi
objects=()
for file in "${FILES[@]}" "$HERE/compat/runtime.c" "$P/Core/Startup/startup_stm32f103zetx.s"; do
 if [[ "$file" = /* ]]; then src="$file"; obj="$O/$(basename "$file").o";
 else src="$P/$file"; obj="$O/$(basename "$file").o"; fi
 echo "compile: ${src##*/}"
 if [[ "$file" == "Core/Src/c3_camera.c" && "${C3_CAMERA_OPT_LEVEL:-3}" != "s" ]]; then
   clang "${CFLAGS[@]}" "-O${C3_CAMERA_OPT_LEVEL:-3}" "${I[@]}" -c "$src" -o "$obj"
 elif [[ "${C3_LTO:-1}" == "1" && ( "$src" == *.s || "$src" == "$HERE/compat/runtime.c" ) ]]; then
   # Startup is machine assembly; runtime exports ARM EABI memcpy/memclr
   # helpers which LTO code generation may only reference AFTER internalization.
   # Keep these helper definitions as normal ELF for the full mainline link.
   # v145 diagnostic also needs __aeabi_memcpy when settings Flash is read.
   # Keep runtime as native ELF in both mainline and diagnostic links.
   clang "${CFLAGS[@]}" -fno-lto "${I[@]}" -c "$src" -o "$obj"
 else
   clang "${CFLAGS[@]}" "${I[@]}" -c "$src" -o "$obj"
 fi
 objects+=( "$obj" )
done
LINK_LTO=()
if [[ "${C3_LTO:-1}" == "1" ]]; then
  # Same link optimization in O2+LTO and O3+LTO: isolates source -O3 effect.
  LINK_LTO=( -flto=full -Wl,--lto-O2 )
fi
clang --target=arm-none-eabi -mcpu=cortex-m3 -mthumb -nostdlib -fuse-ld=lld \
  "${LINK_LTO[@]}" -Wl,--gc-sections -Wl,-Map="$O/c3m1.map" -Wl,--print-memory-usage \
  -T "$HERE/linker_lld.ld" "${objects[@]}" -o "$O/c3m1.elf"
llvm-objcopy -O binary "$O/c3m1.elf" "$O/c3m1.bin"
llvm-objcopy -O ihex "$O/c3m1.elf" "$O/c3m1.hex"
ls -lh "$O/c3m1.elf" "$O/c3m1.bin" "$O/c3m1.hex"

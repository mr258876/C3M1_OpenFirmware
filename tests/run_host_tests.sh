#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
CFLAGS=( -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/C3M1_Camera/Core/Inc" )
# Run legacy tests against the explicit old-timing switch.
CFLAGS+=( -DC3_SHUTTER_DYNAMIC_TIMING=0 )

# v168: actual SDIO DMA wrapper under injected HAL/BSP timing and errors.
cc "${CFLAGS[@]}" -I"$ROOT/tests" -DC3_SDIO_TX_DMA=1 -DC3_SD_DMA_HOST_TEST \
   tests/test_v168_sd_dma.c C3M1_Camera/Core/Src/c3_sd_dma_write.c \
   -o "$OUT/test_v168_sd_dma"
"$OUT/test_v168_sd_dma"
python3 tests/test_v168_dma_contract.py
python3 tests/test_v170_exhcl_contract.py
# v172: actual RAW AWB freeze + shutter-time OSD (EXHCL-aware OpenMV adaptation).
cc "${CFLAGS[@]}" tests/test_v172_raw_awb.c \
   C3M1_Camera/Core/Src/c3_dng.c C3M1_Camera/Core/Src/c3_exposure.c \
   -o "$OUT/test_v172_raw_awb"
"$OUT/test_v172_raw_awb"
for exh in 0x9E 0x80; do
  cc "${CFLAGS[@]}" -DC3_RGB_EXHCL="$exh" tests/test_v172_shutter_display.c \
    C3M1_Camera/Core/Src/c3_exposure.c C3M1_Camera/Core/Src/c3_meter.c \
    -o "$OUT/test_v172_shutter_display"
  "$OUT/test_v172_shutter_display"
done
python3 tests/test_v172_awb_contract.py
for exh in 0x9E 0x80 0x40 0x00; do
  cc "${CFLAGS[@]}" -DC3_RGB_EXHCL="$exh" tests/test_v170_exhcl_timing.c \
    C3M1_Camera/Core/Src/c3_exposure.c -o "$OUT/test_v170_exhcl"
  "$OUT/test_v170_exhcl"
done
# v169: full C implementation, correct row ratios and zoom-vs-BMP distinction.
cc "${CFLAGS[@]}" tests/test_v169_datasheet_exposure.c \
   C3M1_Camera/Core/Src/c3_exposure.c -o "$OUT/test_v169_datasheet_exposure"
"$OUT/test_v169_datasheet_exposure"
# v177: new production timing tables, both FAST and SAFE 4X alternatives.
for fast in 0 1; do
  cc -std=c11 -O2 -Wall -Wextra -Werror -I"$ROOT/C3M1_Camera/Core/Inc" \
    -DC3_RGB_EXHCL=0x80 -DC3_SHUTTER_DYNAMIC_TIMING=1 -DC3_4X_FAST_8000="$fast" \
    tests/test_v177_mainline_timing.c C3M1_Camera/Core/Src/c3_shutter_timing.c \
    C3M1_Camera/Core/Src/c3_exposure.c -o "$OUT/test_v177_timing_$fast"
  "$OUT/test_v177_timing_$fast"
done
python3 tests/test_v177_mainline_integration.py
for name in focus fps buttons frame_sched exposure zoom meter; do
  cc "${CFLAGS[@]}" "tests/test_${name}.c" "C3M1_Camera/Core/Src/c3_${name}.c" -o "$OUT/test_${name}"
  "$OUT/test_${name}"
done
cc "${CFLAGS[@]}" -DC3_INPUT_HOST_TEST tests/test_input.c \
   C3M1_Camera/Core/Src/c3_input.c C3M1_Camera/Core/Src/c3_buttons.c \
   -o "$OUT/test_input"
"$OUT/test_input"
cc "${CFLAGS[@]}" -DC3_BATTERY_HOST_TEST tests/test_v158_battery.c \
   C3M1_Camera/Core/Src/c3_battery.c -o "$OUT/test_v158_battery"
cc "${CFLAGS[@]}" -DC3_SLEEP_HOST_TEST tests/test_v160_sleep.c \
   C3M1_Camera/Core/Src/c3_sleep.c -o "$OUT/test_v160_sleep"
"$OUT/test_v160_sleep"
python3 tests/test_v160_sleep_contract.py
# v0.1.61: status LED lights for actual SD sector writes, not reads/idle.
cc "${CFLAGS[@]}" -DC3_SLEEP_HOST_TEST tests/test_v161_sd_led.c \
   C3M1_Camera/Core/Src/c3_sleep.c -o "$OUT/test_v161_sd_led"
"$OUT/test_v161_sd_led"
cc "${CFLAGS[@]}" -Wno-unused-parameter -I"$ROOT/tests/v161_sd_mock" \
   tests/test_v161_sd_write_driver.c \
   C3M1_Camera/Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c \
   -o "$OUT/test_v161_sd_write_driver"
"$OUT/test_v161_sd_write_driver"
python3 tests/test_v161_sd_led_contract.py
cc "${CFLAGS[@]}" tests/test_v158_font.c C3M1_Camera/Core/Src/c3_font.c \
   -o "$OUT/test_v158_font"
"$OUT/test_v158_font"
"$OUT/test_v158_battery"
cc "${CFLAGS[@]}" tests/test_v158_font.c C3M1_Camera/Core/Src/c3_font.c \
   -o "$OUT/test_v158_font"
"$OUT/test_v158_font"
cc "${CFLAGS[@]}" tests/test_ui_nav.c \
   C3M1_Camera/Core/Src/c3_ui_nav.c C3M1_Camera/Core/Src/c3_settings.c \
   -o "$OUT/test_ui_nav"
"$OUT/test_ui_nav"
cc "${CFLAGS[@]}" -I"$ROOT/tests/flash_mock" \
   tests/test_flash_migration.c \
   C3M1_Camera/Core/Src/c3_settings.c C3M1_Camera/Core/Src/c3_settings_flash.c \
   -o "$OUT/test_flash_migration"
"$OUT/test_flash_migration"
# Standalone OSD unit test has no DMA hardware mock; disable only here.
cc "${CFLAGS[@]}" -DC3_LCD_DMA_ENABLE=0 tests/test_osd.c \
   C3M1_Camera/Core/Src/c3_osd.c C3M1_Camera/Core/Src/c3_font.c \
   C3M1_Camera/Core/Src/c3_exposure.c C3M1_Camera/Core/Src/c3_meter.c \
   -o "$OUT/test_osd"
"$OUT/test_osd"
cc "${CFLAGS[@]}" tests/test_dng.c \
   C3M1_Camera/Core/Src/c3_dng.c -o "$OUT/test_dng"
"$OUT/test_dng" "$OUT/test.dng"
cc "${CFLAGS[@]}" tests/test_v164_dng_awb.c \
   C3M1_Camera/Core/Src/c3_dng.c -o "$OUT/test_v164_dng_awb"
"$OUT/test_v164_dng_awb"
cc "${CFLAGS[@]}" tests/test_v165_tint.c \
   C3M1_Camera/Core/Src/c3_dng.c -o "$OUT/test_v165_tint"
"$OUT/test_v165_tint"
# Generate a genuine v0.1.64 test header for metadata-only repair validation.
cc "${CFLAGS[@]}" -DC3_DNG_TINT_REVERSE=0 -DC3_DNG_CALIBRATION_D50=0 \
   tests/test_dng.c C3M1_Camera/Core/Src/c3_dng.c -o "$OUT/test_dng_v164"
"$OUT/test_dng_v164" "$OUT/test_v164.dng"
python3 tests/test_v165_repair.py "$OUT/test_v164.dng"
python3 tests/test_v164_matrix.py "$OUT/test.dng"
python3 tests/test_v164_capture_contract.py
cc "${CFLAGS[@]}" -I"$ROOT/tests" -DC3_STORAGE_HOST_TEST \
   tests/test_storage_retry.c C3M1_Camera/Core/Src/c3_storage.c \
   C3M1_Camera/Core/Src/c3_dng.c C3M1_Camera/Core/Src/c3_bmp.c \
   C3M1_Camera/Core/Src/c3_capture_recovery.c \
\
   -o "$OUT/test_storage_retry"
"$OUT/test_storage_retry" "$OUT/test.bmp"
# v0.1.37: actual optimized row helpers vs reference on 3000 random input rows.
cc "${CFLAGS[@]}" tests/test_row_opt37.c -o "$OUT/test_row_opt37"
"$OUT/test_row_opt37"
# v0.1.39: packed 16-bit SRAM writes must match original RGB565 bytes.
cc "${CFLAGS[@]}" tests/test_v39_packed_preview.c -o "$OUT/test_v39_packed_O2"
"$OUT/test_v39_packed_O2"
cc -std=c11 -Os -Wall -Wextra -Werror -fstrict-aliasing \
   tests/test_v39_packed_preview.c -o "$OUT/test_v39_packed_Os"
"$OUT/test_v39_packed_Os"
# v0.1.57: central-ROI peaking must be pixel-identical INSIDE ROI and
# produce the same focus score, without leaking color effects outside ROI.
for opt in 0 1 2; do
  cc "${CFLAGS[@]}" -O3 -DC3_PEAK_OPT="$opt" tests/test_v157_center_peak.c \
     C3M1_Camera/Core/Src/c3_focus.c -o "$OUT/test_v157_center_$opt"
  "$OUT/test_v157_center_$opt"
done
# v0.1.42 exact peaking renderer vs v0.1.41 pixel/state reference.
cc "${CFLAGS[@]}" tests/test_v42_peak_staged.c C3M1_Camera/Core/Src/c3_focus.c \
   -o "$OUT/test_v42_peak_staged"
# v0.1.44: test new peaking SRAM paths against reference pixel + focus state.
for opt in 1 2; do
  cc "${CFLAGS[@]}" -O3 -DC3_PEAK_OPT="$opt" tests/test_v42_peak_staged.c \
     C3M1_Camera/Core/Src/c3_focus.c -o "$OUT/test_v44_peak_$opt"
  "$OUT/test_v44_peak_$opt"
done
# Extra endianness, bounds and unaligned-byte-buffer exercise.
for opt in 0 1 2; do
  cc "${CFLAGS[@]}" -DC3_PEAK_OPT="$opt" tests/test_v44_peak_unaligned.c \
     C3M1_Camera/Core/Src/c3_focus.c -o "$OUT/test_v44_unaligned_$opt"
  "$OUT/test_v44_unaligned_$opt"
done
"$OUT/test_v42_peak_staged"
# v0.1.44: test new peaking SRAM paths against reference pixel + focus state.
for opt in 1 2; do
  cc "${CFLAGS[@]}" -O3 -DC3_PEAK_OPT="$opt" tests/test_v42_peak_staged.c \
     C3M1_Camera/Core/Src/c3_focus.c -o "$OUT/test_v44_peak_$opt"
  "$OUT/test_v44_peak_$opt"
done
# Extra endianness, bounds and unaligned-byte-buffer exercise.
for opt in 0 1 2; do
  cc "${CFLAGS[@]}" -DC3_PEAK_OPT="$opt" tests/test_v44_peak_unaligned.c \
     C3M1_Camera/Core/Src/c3_focus.c -o "$OUT/test_v44_unaligned_$opt"
  "$OUT/test_v44_unaligned_$opt"
done
python3 tests/test_v40_asm_contract.py
# v0.1.38: separately-buffered diagnostic pixels must match RowOpt2 exactly.
cc "${CFLAGS[@]}" tests/test_v38_buffered_mix.c -o "$OUT/test_v38_buffered_mix"
"$OUT/test_v38_buffered_mix"
python3 tests/test_v38_profile_contract.py
python3 tests/check_bmp.py "$OUT/test.bmp"
python3 tests/verify_outputs.py build/c3m1.bin "$OUT/test.dng"
python3 tests/test_sccb_semantics.py
# v0.1.50: execute the ACTUAL low-level SCCB read byte implementation
# with a fake PG13 bus and assert that input ownership/restore is safe.
for rx_mode in 0 1; do
  cc "${CFLAGS[@]}" -DC3_SCCB_FLOAT_READ="$rx_mode" \
     tests/test_v150_sccb_rx_host.c -o "$OUT/test_v150_sccb_rx_$rx_mode"
  "$OUT/test_v150_sccb_rx_$rx_mode"
done
python3 tests/test_rgb_ff_gate.py
python3 tests/test_sccb_speed_probe.py
python3 tests/test_lcd_coldboot.py
python3 tests/test_board_io_and_com3.py
python3 tests/test_v29_lcd_color_contract.py
python3 tests/test_v30_shot_trace_contract.py
# v0.1.31 LCD DMA: test the actual DMA state machine in a host bus mock.
cc "${CFLAGS[@]}" -DC3_LCD_DMA_ENABLE=1 -DC3_LCD_DMA_HOST_TEST=1 \
   -I"$ROOT/tests" tests/test_lcd_dma_mock.c C3M1_Camera/Core/Src/c3_lcd_dma.c \
   -o "$OUT/test_lcd_dma_mock"
for scenario in 0 1 2 3 4; do "$OUT/test_lcd_dma_mock" "$scenario"; done
python3 tests/test_v31_dma_contract.py
python3 tests/test_corrected_pinmap.py
python3 tests/test_bus_probe_contract.py
python3 tests/test_mainline_build.py
if [[ -f build_probe/c3m1.elf ]]; then
  python3 tests/test_fifo_probe22.py
else
  echo 'FIFO diagnostic ELF check skipped; build with C3_PREVIEW_PROBE=1.'
fi
if [[ -f build_preview/c3m1.elf ]]; then
  python3 tests/test_preview_probe.py
else
  echo 'Preview-only ELF check skipped; build with C3_PREVIEW_PROBE=1 C3_FIFO_PROBE=0.'
fi
python3 tests/test_v159_osd_battery_contract.py
echo 'All C3M1 mainline host checks passed.'
python3 tests/test_v145_sccb_fallback.py
# Legacy UI contracts for v0.1.45/46 (ISO changes SCCB speed) do not apply in v0.1.47.
# Their implementation remains in source-control history; v0.1.47 scans neighbors instead.
# v0.1.47 page-toggle/read-only contract is historical: the new page 4
# explicitly writes temporary sensor registers on an intentional shutter press.
python3 tests/test_v148_bit7_probe.py
python3 tests/test_v149_phase_probe.py
# v0.1.62 capture speedup: pure snapshot + row conversion, register write ordering.
cc "${CFLAGS[@]}" tests/test_v162_exposure.c \
    C3M1_Camera/Core/Src/c3_exposure.c -o "$OUT/test_v162_exposure"
"$OUT/test_v162_exposure"
python3 tests/test_v162_capture_contract.py

# v166 preview exposure nonreset + fast RAW differential source coverage.
cc "${CFLAGS[@]}" tests/test_v166_preview_controls.c \
    C3M1_Camera/Core/Src/c3_exposure.c C3M1_Camera/Core/Src/c3_frame_sched.c \
    -o "$OUT/test_v166_preview_controls"
"$OUT/test_v166_preview_controls"
python3 tests/test_v166_fastpath_contract.py

# v167 experimental no-reset RGB<->RAW<->RGB round trip.
cc "${CFLAGS[@]}" tests/test_v167_preview_resume.c \
    C3M1_Camera/Core/Src/c3_exposure.c -o "$OUT/test_v167_preview_resume"
"$OUT/test_v167_preview_resume"
python3 tests/test_v167_rgb_switch_contract.py

# v178 BLC register round-trip / strict capture gate (both A/B builds).
python3 tests/test_v178_blc.py

# C3M1 OpenFirmware — development guide

Project: https://github.com/mr258876/C3M1_OpenFirmware
Copyright (C) 2026 mr258876 · AGPL-3.0-only. See [LICENSE](LICENSE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Baseline and workflow

`main` is the single production firmware (v0.2.21, ACR8 F2). Run `bash build.sh` from the repository root. Clang 17, LLD, `llvm-objcopy`, Cortex-M3 Thumb, freestanding C11, full LTO and `-Os` are the baseline; the camera translation unit uses `-O3`. The linker reserves 508 KiB for code and 64 KiB for RAM. Build products belong in `build/`.

The `dev` branch preserves the pre-cleanup experimental configurations and host tests. Its build scripts and test fixtures match that snapshot. Keep future experiments on `dev`; port validated changes to the production tree rather than restoring every historical compile-time switch. Keep experimental snapshots out of the main branch. The CI workflow builds both branches using LLVM 17; verify uploaded artifacts before publishing a release.

## Source map

| Area | Files under `C3M1_Camera/Core/` |
| --- | --- |
| Boot and application loop | `Src/main.c`, `Inc/c3_boot_timing.h`, `Inc/c3_startup_hint.h` |
| Board and interrupts | `Inc/c3_board_pins.h`, `Src/gpio.c`, `Src/stm32f1xx_it.c`, `Src/fsmc.c`, `Src/sdio.c` |
| Sensor / FIFO / preview | `Src/c3_camera.c`, `Src/c3_fifo_dma_live.inc`, `Src/c3_frame_sched.c`, `Inc/c3_fifo_dma_live_plan.h`, `Inc/c3_preview_dma_render.h` |
| LCD and graphics | `Src/c3_lcd.c`, `Src/c3_lcd_dma.c`, `Src/c3_osd.c`, `Src/c3_font.c`, `Inc/c3_preview_*` |
| Focus assistance | `Src/c3_focus.c`, `Inc/c3_peaking.h` |
| Exposure and magnification | `Src/c3_exposure.c`, `Src/c3_shutter_timing.c`, `Src/c3_meter.c`, `Src/c3_zoom.c` |
| Capture and storage | `Src/c3_storage.c`, `Src/c3_capture_recovery.c`, `Src/c3_dng.c`, `Src/c3_bmp.c`, `Src/c3_sd_dma_write.c` |
| Photo display | `Src/c3_playback.c`, `Src/c3_play_decode.c`, `Inc/c3_postshot.h` |
| UI and input | `Src/c3_menu.c`, `Src/c3_ui_nav.c`, `Src/c3_input.c`, `Src/c3_buttons.c`, `Inc/c3_ui_hints.h` |
| Preferences and power | `Src/c3_settings.c`, `Src/c3_settings_flash.c`, `Src/c3_sleep.c`, `Src/c3_battery.c` |
| Localization | `Src/c3_i18n.c`, `Src/c3_i18n_data.inc`, `tools/i18n_zh.json` |

The sensor driver lives in `Drivers/BSP/OV7725`; the SD block-device integration is in `FATFS/Target` and `Middlewares/Third_Party/FatFs/src/drivers`. The vendor directories retain their copyright notices.

## Board and timing contracts

- PB3 = FIFO WEN, PB4 = RCLK, PG15 = active-low output enable, PG14 = read reset, PD6 = write reset, PG1–PG8 = data. PA8 receives VSYNC. PB3/PB4 require JTAG disabled with SWD retained.
- PF1 = ISO, PF2 = shutter, PF3 = 4X, PF10 = film lever. All are active-low inputs. PF4 controls LCD enable and PF5 resets LCD. PF6–PF9 read the dial. PA1 measures battery voltage; PD9 drives the active-low status LED via open drain.
- Boot uses 500 ms LCD and 1000 ms sensor power-settle windows with overlap. SCCB uses the validated floating-input receive path and a 50 µs per-edge delay. Preserve the reset and readback sequence when changing these timings.
- The preview scheduler owns the AL422B ring across VSYNC events. Keep write-pointer resets outside the ISR, keep interrupts short, and rearm the ring after a failed DMA transfer. DMA1 Channel 2 reads GPIOG; TIM3 generates RCLK at 72 MHz/14, TIM4 limits each four-row burst. DMA2 Channel 1 sends LCD data. SDIO uses DMA2 Channel 4 for transmit. Keep peripheral polling outside active FIFO DMA transfers.
- A preview block contains 2,560 FIFO bytes; its nominal transfer budget is about 498 µs. Preserve the 16-bit GPIO input packing and MSB-first RGB565 LCD order. CPU fallback is part of production recovery, not diagnostic code.
- Peak gradients use green-channel values from original pixels on a fixed 2×2 grid, with 2×2 highlight expansion; the shared score uses the aligned 4×4 subset. The adaptive threshold ROI is central 160×120. Never feed overlay pixels back into gradient computation.

## Exposure and file integrity

The shutter dial chooses the mode with gain; the three hardware timing tables are independent for RGB1X, RGB4X and RAW8. Use `c3_shutter_timing.c` for row-period and dummy-pixel changes. P/S warnings compare measured luminance and actual exposure/gain limits. Clear Night Mode and vertical dummy rows in S/M. In 4X, freeze AEC/AGC/AWB and wait for VSYNC before verifying the snapshot. On failure restore 1X; do not relax register verification. 4X capture still saves the full field.

The sensor produces 644 RAW bytes per row. Store the first 642 for each of 480 rows, not a linear truncation. BGGR order, Orientation, strip offsets, white level, scalar black-level metadata and the embedded Soft M675 profile describe the stored bytes. Metadata is captured once per shot and preserved across storage retries. The F2 path waits two complete VSYNC frames when the fast RAW lock succeeds; recovery paths retain the longer settling sequence.

Storage commits a complete temporary file before publishing its final numbered filename. Retain the held FIFO frame on failure until retry succeeds or the user discards it. Use the exact committed filename for instant review. Display the lever prompt immediately when both post-shot options are enabled. A fresh lever edge, not a previously held key, releases the wind wait.

## UI and persistent settings

Keep menus bilingual; `tools/i18n_zh.json` maps English keys to Simplified Chinese. The generated C array contains only the 16×16 glyphs used by the firmware. Regenerate after changing translations (Python 3 plus fontTools; obtain AR PL UMing CN from its upstream distribution):

```sh
python3 tools/gen_i18n_bitmap.py --font /path/to/uming.ttc --index 0
```

Check 320×240 layout with the actual 5×7 ASCII and 16×16 Chinese cells. Keep live OSD on its existing pixel-composition path. During language selection use the highlighted language for local hints; mandatory selection commits before continuing. Startup hints start on the first complete preview frame after setup/reset.

`c3_settings_t` is 12 bytes; v6 records are 32 bytes with CRC and commit word. Flash pages A/B are `0x0807F000` and `0x0807F800`. Write the inactive page and its commit word last. Preserve field layout, numeric enum values and the legacy `peak_area` byte (now language, hint, review and wind bits). Change the persisted record version only with a deliberate compatibility plan. Factory reset leaves photos intact and marks language as pending.

Recreate the black-level table with the bundled 45-setting measurement CSV:

```sh
python3 tools/generate_v179_black_lut.py --output C3M1_Camera/Core/Src/c3_dng_black_lut.inc
```

Retain the attribution and license attached to the bitmap subset and vendor dependencies.

## Verification checklist

For code changes, build the target, check `c3m1.map` against Flash/RAM limits, and inspect changes to timing-sensitive generated instructions. Run the relevant `dev` host tests or adapt those fixtures to the new interfaces. For sensor, FIFO or SD changes, test on hardware: cold boot, all exposure modes, 1X/4X, normal and low light, image orientation and colors, repeated BMP/DNG saves, interrupted SD writes, playback, and both post-shot options. Check language/reset behavior and Flash retention when changing settings. Record measured FPS and error codes alongside the change. Keep generated binaries and local test images out of source commits.

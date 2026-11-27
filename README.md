# C3M1 OpenFirmware

[简体中文](README.zh-CN.md) · [Change list](CHANGELOG.md) · [Developer guide](Agent.md)

C3M1 OpenFirmware is a replacement firmware for the C3M1 digital camera. It gives the camera direct shutter and gain controls, RAW DNG capture, focus assistance, photo playback and a bilingual camera interface while retaining the original hardware.

**Current version: v0.2.21.**

## Features

- P, S and M exposure modes selected by the physical shutter dial and ISO button; nine manual shutter settings from 1/50 to 1/8000 s, AUTO and 1×–16× gain, and ±3 EV compensation in half-stop steps.
- 642×480 BGGR RAW DNG with per-shot white balance, calibrated black-level metadata and the embedded C3M1 Soft M675 profile; 320×240 24-bit BMP capture.
- 1× live view and 4X focus magnification with exposure/white-balance lock. Photos retain the full field of view.
- Full-frame sparse focus peaking, shared contrast score, optional grid, monochrome view and FPS display. The FIFO, CPU and LCD form a pipelined preview path.
- Photo browsing, instant review and optional film-lever advance after each shot. Failed saves can be retried from the held FIFO image.
- English and Simplified Chinese menus, native bitmap glyphs, physical-button icons, battery voltage and estimated charge, configurable sleep, factory reset and FAT card formatting.

## Hardware

| Component | Specification |
| --- | --- |
| MCU | STM32F103ZET6, Cortex-M3, 72 MHz |
| Memory | 512 KiB Flash, 64 KiB SRAM |
| Sensor | OmniVision OV7725 |
| Video FIFO | AL422B, 384 KiB |
| LCD | ST7789V3, 320×240 RGB565, FSMC |
| Storage | SD card via SDIO / FatFs |

The production firmware uses four-row FIFO DMA at 72 MHz / 14 (about 5.14 MHz), LCD DMA and SDIO transmit DMA. The final focus pipeline reached 27.3 FPS with peaking and scoring both enabled in the v0.2.03 hardware test. Still capture uses the CPU FIFO reader.

## Build

Install Bash, Clang 17, LLVM LLD and `llvm-objcopy`. No CubeMX, STM32CubeIDE, Keil, Python package or external font is needed for a normal build. The required embedded headers, driver sources, runtime helpers, generated glyphs and calibration table are included.

```sh
bash build.sh
```

Outputs:

- `build/c3m1.hex` — Intel HEX for programming
- `build/c3m1.bin` — raw image at `0x08000000`
- `build/c3m1.elf` — symbols for debugging
- `build/c3m1.map` — memory layout

The build uses the single production configuration (ACR8 F2). `CLANG` and `LLVM_OBJCOPY` can select alternate executable paths. See [Agent.md](Agent.md) for the source map and hardware contracts.

## Install

Use an SWD programmer with GND, SWDIO, SWCLK and the correct target voltage reference. Back up the entire original 512 KiB Flash before erasing it. Program and verify the HEX file at its embedded addresses, then reset the camera. For BIN programming, use base address `0x08000000`.

The last two 2 KiB Flash pages (`0x0807F000`, `0x0807F800`) store settings. Erase these pages only to reset stored preferences or as part of an intentional full-chip restore. To retain preferences, use page/sector erase for the application region instead of mass erase.

On a clean installation, select a language with the shutter dial and confirm with 4X.

## Controls

| Control | Live view |
| --- | --- |
| Shutter dial | Select shutter speed; remaining positions select AUTO |
| ISO (short press) | Cycle AUTO → 1× → 2× → 4× → 8× → 16× with manual shutter |
| ISO (hold) | Toggle focus peaking |
| 4X | Enter or leave focus magnification; AE-L indicates the locked exposure |
| Shutter | Capture |
| Film lever (hold) | Open the function menu |
| Film lever (tap) | Advance after a shot when WIND AFTER SHOT is enabled |

| Shutter | Gain | Mode |
| --- | --- | --- |
| AUTO | AUTO | P |
| Manual | AUTO | S |
| Manual | Manual | M |

In P mode, the ISO short press does not select manual gain. Moving the dial into AUTO returns gain to AUTO. The shutter dial is reused for menu navigation; check its physical position when returning to live view.

**Function menu:** turn the dial to change EV, press 4X for Settings, ISO for Playback, or Shutter to return. The header shows battery voltage and estimated charge.

**Settings:** turn the dial to select an item; press 4X to set or open it. Press Shutter to exit. Language selection uses the dial and 4X; the first-run and factory-reset language page requires confirmation before proceeding.

**Playback:** turn the dial to browse numbered BMP/DNG files, press ISO to toggle photo information, or Shutter to return to live view. DNG playback uses a streamed Bayer preview with the file's metadata.

## Defaults and post-shot behavior

| Setting | Default |
| --- | --- |
| Focus peaking / color / sensitivity | ON / red / high |
| Focus score | ON |
| FPS display / grid / monochrome | OFF |
| OSD | ON |
| Output | RAW DNG |
| LCD rotate 180° | ON |
| Auto sleep | 1 minute |
| Startup hint | ON |
| Instant review | ON |
| Wind after shot | ON |
| Language | Select at first run |

| Instant review | Wind after shot | After a successful save |
| --- | --- | --- |
| OFF | OFF | Return to live view |
| ON | OFF | Display the saved photo for five seconds; any key exits early |
| OFF | ON | Keep the last preview frame, display save confirmation and a lever prompt; wait for the lever |
| ON | ON | Display the saved photo and lever prompt immediately; wait for the lever |

Settings are retained across normal boots. Factory reset restores the defaults and opens language selection without deleting photos.

## Storage and image files

Files use the `C3_00001.DNG` / `C3_00001.BMP` naming scheme with a shared sequence. Existing files are not overwritten. The Format Card action creates a single FAT partition on cards up to 32 GiB; back up the card before confirming, because formatting removes its files. If saving fails, use the on-screen retry or discard controls.

The RAW format is an 8-bit sensor stream, not an expanded 16-bit image. The DNG crop removes two trailing bytes per sensor row and preserves the CFA phase. Exposure and color are described by the DNG metadata, leaving the Bayer samples intact.

## Project layout

```text
C3M1_Camera/Core/        Application, UI, capture, exposure, preview
C3M1_Camera/Drivers/     Sensor, platform, CMSIS and HAL dependencies
C3M1_Camera/FATFS/       SD/FatFs integration
C3M1_Camera/Middlewares/ FatFs implementation
compat/                 Bare-metal runtime support
tools/                 Translation/glyph and black-level table generation
build.sh                Production build
linker_lld.ld           Application and settings Flash layout
```

The change list records the accumulated development work. [Third-party notices](THIRD_PARTY_NOTICES.md) identify the retained dependencies and their licenses.

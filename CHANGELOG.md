# Change list — v0.1.0–v0.2.21

Development from v0.1.0 through v0.2.21, grouped by subsystem. The earliest phase is reconstructed from surviving source, host-test descriptions and patches; individual pre-v0.1.24 release boundaries are not assigned where records are missing.

## 1. Firmware foundation and board bring-up (v0.1.x)

- Rebuilt the camera application around a bare-metal Cortex-M3 build, with modular input, settings, display, exposure, capture and storage code. Added P/S/M exposure handling, rotary navigation, 4X crop geometry, streamed previews, focus assistance and CRC-protected settings.
- Reworked GPIO assignments against the production schematics: corrected FIFO WEN/RCLK/OE, ISO, shutter, 4X, LCD enable and reset. Disabled JTAG while retaining SWD. Fixed camera-image orientation and RGB channel order.
- Stabilized cold boot with controlled LCD reset/backlight sequencing, SCCB initialization and sensor identification, repeated reads and VSYNC validation. Corrected SCCB high-bit read errors by switching the receive phase to floating input and added bounded read recovery. Adopted the working PLL4 sensor timing after color-band experiments.
- Established streamed FIFO/LCD rendering and frame scheduling, DMA-driven LCD transfer, bounded interrupt work and ring-pointer recovery. Added common BMP/DNG numbering, retryable storage transactions and protected held-frame handling.

## 2. Capture and color pipeline (v0.1.55–v0.1.82)

- Corrected DNG orientation and BGGR CFA metadata; removed two trailing columns per 644-byte sensor row to produce 642×480 RAW8. Added dynamic per-shot AsShotNeutral from preview AWB, tint polarity correction, D50 matrix declaration, ColorMatrix/ForwardMatrix and the embedded nine-point Soft M675 profile.
- Accelerated exposure changes and RGB↔RAW format switching with differential register updates and full initialization recovery. Froze AWB before RAW conversion and preserved exposure and color metadata across write retries. Adopted SDIO transmit DMA.
- Derived separate shutter plans for RGB1X, RGB4X and RAW8 using sensor row timing, clock divider and horizontal dummy pixels. Matched the nine dial positions to the three timing tables and updated OSD exposure estimates.
- Restored RAW automatic black-level correction; explicitly restored BLC state after format switching. Measured 45 gain/shutter combinations before and after the fix. Zero-valued black-frame pixels fell from about 45.1% to 0.034% in that dataset. Added the 5×9×4 calibration table, selected by exposure and gain, and fractional DNG black metadata.
- Replaced repeated FIFO captures during settling with VSYNC frame counting; selected the two-frame fast path with longer recovery waits. Resolved Camera Raw compatibility issues with multichannel BlackLevel in RAW8; selected the ACR8 scalar mean rather than doubling storage to RAW16.

## 3. Exposure controls, information and reliability (v0.1.57–v0.1.88, v0.2.04–v0.2.09)

- Added battery-voltage sensing and a charge estimate, battery OSD, auto sleep with LED breathing, LCD sleep/wake and safe handling of unsaved photos.
- Enlarged and rearranged exposure OSD into shutter, photo counter, EV and gain; folded SD status into a flashing red NO CARD indicator. Added small AUTO labels and exposure-limit blinking based on actual luminance and gain/exposure readbacks.
- Moved P/S/M selection onto the physical shutter dial and ISO button, retained direct gain cycling and long-ISO peak toggle, corrected EV dial direction, made the function menu persistent until explicit exit, and shortened sleep choices to 15 s / 30 s / 1 min / 3 min / off.
- Corrected Night Mode carryover into S/M by clearing additional vertical timing rows and validating the final registers. Prevented transient Night Mode enable during RGB initialization. Added exposure/WB lock for 4X focus magnification and preserved full-field capture with locked parameters. Refined lock synchronization and bounded retry.
- Consolidated top OSD, removed redundant grid status, introduced AE-L and compact FS/FPS labels. Overlapped LCD and sensor power-up waits; adopted 500 ms LCD and 1000 ms CMOS minimum waits.

## 4. Preview and focus performance (v0.1.89–v0.2.03)

- Separated focus score from peak rendering. Compared central/full, dense/sparse, luminance LUT, frame scheduling, green-channel and pixel expansion methods. Chose fixed 2×2 sparse peaking with 2×2 highlight expansion instead of alternating sampling, which visibly flickered.
- Explored CPU FIFO assembly and packed writes, then implemented timer-gated FIFO DMA. Used frozen-frame CRC and byte checks, request phase and frequency sweeps, quiet versus polling waits, and concurrent CPU/LCD load measurements to choose 5.14 MHz/T1 with four-row blocks. Rejected unstable 6 MHz block mode and retained CPU recovery.
- Measured per-block CPU headroom. Unified score and peak brightness on the green channel, aligned the score grid to existing peak gradients, and expanded sparse coverage. Compared DMA unpacking variants; retained the original unpack path.
- Removed dense and central peak modes and redundant score choices. The final peak covers the live image between OSD bands; the score uses a 4×4 subset, reusing peak gradients when enabled. All four on/off combinations reached 27.3 FPS in v0.2.03 hardware measurements.

## 5. Playback, localization and menu system (v0.2.07–v0.2.18)

- Added mixed BMP/DNG browsing with streamed decoding, file metadata, previous/next controls and optional information overlay. Supported numbering gaps and historical RAW container variants.
- Introduced English and Simplified Chinese menus, native 16×16 Chinese bitmaps and icons representing physical controls. Moved battery readout to the function menu header, added About and the C3M1 OpenFirmware project name, and replaced technical EV text with an approximate-compensation notice.
- Added confirmed factory reset and FAT formatting, with card capacity and write-error checks. Added five-second startup instructions and a persistent enable switch.
- Replaced language cycling with a separate selector. First boot and factory reset require language confirmation before normal use; interrupted confirmation remains pending. The selector retains English anchors while other hints follow the highlighted language. Settings retain the v6 record layout with CRC and dual-page commit.
- Changed defaults to red high-sensitivity peak and score ON, FPS/grid/monochrome OFF, OSD and rotation ON, RAW DNG output, one-minute sleep and startup hint ON.

## 6. Post-shot workflow (v0.2.19–v0.2.21)

- Added independent Instant Review and Wind After Shot preferences, both enabled by default. Review-only displays the committed photo for five seconds and exits on a new key; wind-only waits for a fresh film-lever trigger.
- With both options enabled, display the saved photo with the wind prompt immediately; ignore other keys. Apply the same flow after a successful SD retry. Start review timing only after decoding, consume modal key events and preserve the held photo on failed save.
- Preserve the final live-view frame under the save-confirmation and lever prompt in wind-only mode. Remove the unnecessary full-screen gray fill without re-reading the FIFO or SD card.

## 7. Repository consolidation (v0.2.21)

- Use a single build for the selected production configuration. Remove alternate firmware branches, unused application modules, obsolete IDE metadata, old build wrappers, duplicate reports and test code from the public source tree.
- Keep the production recovery paths, settings compatibility, source dependencies, translation and calibration generators. Store the pre-cleanup experiments and test fixtures on a separate local development branch.
- Provide the user README, development guide, this consolidated change list and dependency notices as the maintained documentation set.
- Add the project identity, author, canonical repository URL and AGPL-3.0 license to the About page and release documentation; consolidate developer instructions into `AGENTS.md`.

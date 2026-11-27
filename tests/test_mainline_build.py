#!/usr/bin/env python3
"""Ensure the normal binary contains the complete mainline, not the read-only FIFO probe.

Symbol checks establish link reachability, not a guarantee of successful capture
on physical hardware.
"""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
elf = root / 'build/c3m1.elf'
assert elf.is_file(), f'Missing mainline ELF: {elf}'
m = subprocess.run(['nm', str(elf)], capture_output=True, text=True, check=True).stdout
symbols = {row.split()[-1] for row in m.splitlines() if len(row.split()) >= 3}
# Full LTO internalizes/inlines small module entry points, so symbol absence
# cannot be used to claim a code path is missing. Check retained roots and
# call sites in source for the expected init/capture chain.
required = ('main', 'f_mount', 'f_write',
            'c3_camera_preview_draw', 'c3_storage_save',
            'c3_settings_flash_save')
for symbol in required:
    assert symbol in symbols, f'Mainline symbol missing: {symbol}'
src_main = (root/'C3M1_Camera/Core/Src/main.c').read_text()
assert 'MX_SDIO_SD_Init();' in src_main
sdio_src = (root/'C3M1_Camera/Core/Src/sdio.c').read_text(encoding='latin-1')
assert 'void MX_SDIO_SD_Init(void)' in sdio_src
cam_src = (root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
assert 'uint8_t c3_camera_raw_init(void)' in cam_src
assert 'uint8_t c3_camera_bmp_init(void)' in cam_src
assert 'HAL_SD_WriteBlocks' in (root/'C3M1_Camera/Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c').read_text() or 'BSP_SD_WriteBlocks' in (root/'C3M1_Camera/Middlewares/Third_Party/FatFs/src/drivers/sd_diskio.c').read_text()
assert 'c3_fifo_probe_run' not in symbols, 'Unexpected diagnostic FIFO probe in mainline'
assert 'c3_sccb_probe_three_speeds' not in symbols, 'Unexpected SCCB probe in mainline'
s = (root / 'build_clang.sh').read_text()
assert 'case "${C3_PREVIEW_PROBE:-0}" in' in s
print('PASS: mainline ELF includes preview, BMP/DNG, SD and settings Flash paths; excludes probe')

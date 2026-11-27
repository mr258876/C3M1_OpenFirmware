#!/usr/bin/env python3
"""v0.1.44 linker/build metadata checks; never a substitute for hardware testing."""
from pathlib import Path
import subprocess
import struct
import hashlib
root = Path(__file__).resolve().parents[1]
release = root / 'release_v144'
build = (root/'build_clang.sh').read_text()
assert 'C3_CAMERA_OPT_LEVEL:-3' in build
assert 'C3_LTO:-1' in build
assert 'C3_OVERLAY_OPT:-3' in build
assert 'C3_PEAK_OPT:-0' in build
assert 'compat/runtime.c' in build and '-fno-lto' in build
reference = 'c3e2e00e62bb811c2bf012cdbc6f8a7406e4676558a8126bfb86f4b2da2fe75f'
for variant in ('BASE', 'P1_PIXEL16', 'P2_GRAD'):
    for kind in ('ReadOnly','Mainline'):
        p = release / f'C3M1_v0_1_44_{kind}_{variant}'
        for ext in ('bin','hex','elf','map'):
            assert p.with_suffix('.'+ext).is_file(), p
        data=p.with_suffix('.bin').read_bytes()
        assert 4096 < len(data) < 508*1024
        stack, reset = struct.unpack_from('<II',data)
        assert stack == 0x20010000, (kind,variant,hex(stack))
        assert 0x08000001 < reset < 0x08080000 and reset & 1, (kind,variant,hex(reset))
        symbols=subprocess.check_output(['nm',str(p.with_suffix('.elf'))],text=True)
        if kind == 'ReadOnly':
            for symbol in (' f_write\n',' f_mount\n',' c3_storage_save\n',' c3_camera_raw_init\n',
                           ' c3_camera_bmp_init\n',' c3_settings_flash_save\n',' HAL_FLASH_Program\n'):
                assert symbol not in symbols, (variant,symbol)
        else:
            for symbol in (' f_write\n',' f_mount\n',' c3_storage_save\n',
                           ' c3_settings_flash_save\n',' HAL_FLASH_Program\n'):
                assert symbol in symbols, (variant,symbol)
        print(f'PASS {kind:8s} {variant:10s}: FLASH={len(data)} B')
    if variant == 'BASE':
        path=release/'C3M1_v0_1_44_ReadOnly_BASE.bin'
        assert hashlib.sha256(path.read_bytes()).hexdigest()==reference, 'field-tested D3 O3+LTO reference mismatch'
print('PASS: D3 O3+LTO byte-exact diagnostic base, full firmware and safe diagnostic paths')

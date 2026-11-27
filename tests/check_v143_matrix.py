#!/usr/bin/env python3
"""Validate four ARM D3 preview-only optimization builds (not a hardware test)."""
from pathlib import Path
from hashlib import sha256
import subprocess, struct

root = Path(__file__).resolve().parents[1]
release = root / 'release_v143'
ref_sha = '13389ef187d25f3e4414792ce7b2548a37d604e980316e99ef3dabd9b51c5dcf'
seen = set()
for tag, opt, lto in [('O2', 2, False), ('O3', 3, False),
                      ('O2_LTO', 2, True), ('O3_LTO', 3, True)]:
    d = root / ('build_preview_' + tag)
    b = d / 'c3m1.bin'
    elf = d / 'c3m1.elf'
    obj = d / 'c3_camera.c.o'
    for f in (b, elf, obj):
        assert f.is_file() and f.stat().st_size > 100, str(f)
    data = b.read_bytes()
    sp, reset = struct.unpack_from('<II', data, 0)
    assert sp == 0x20010000, f'{tag}: invalid initial SP {sp:#x}'
    assert 0x08000000 <= reset & ~1 < 0x08080000 and reset & 1, f'{tag}: invalid reset vector {reset:#x}'
    assert len(data) < 512*1024, f'{tag}: flash overflow'
    digest = sha256(data).hexdigest()
    assert digest not in seen, f'{tag}: build identical to an unintended other variant'
    seen.add(digest)
    if tag == 'O2':
        assert digest == ref_sha, f'O2 baseline differs from v0.1.42 D3: {digest}'
    obj_header = obj.read_bytes()[:4]
    if lto:
        assert obj_header == b'BC\xc0\xde', f'{tag}: LTO did not produce LLVM bitcode'
    else:
        assert obj_header == b'\x7fELF', f'{tag}: non-LTO build is not an ELF object'
    result = subprocess.run(['nm', str(elf)], capture_output=True,
                            text=True, check=True)
    symbols = {line.split()[-1] for line in result.stdout.splitlines() if len(line.split()) >= 3}
    forbidden = {'f_write', 'f_mount', 'f_sync', 'c3_storage_save',
                 'c3_settings_flash_save', 'c3_camera_raw_init',
                 'c3_camera_bmp_init', 'HAL_SD_WriteBlocks', 'HAL_FLASH_Program'}
    assert not (symbols & forbidden), f'{tag}: write-capable symbols: {symbols & forbidden}'
    assert 'c3_camera_preview_draw' in symbols and 'main' in symbols, f'{tag}: preview path missing'
    for ext in ('hex','bin','elf','map'):
        copied = release / f'C3M1_v0_1_43_D3_{tag}.{ext}'
        assert copied.is_file(), str(copied)
        if ext in ('bin', 'elf'):
            assert copied.read_bytes() == (d / f'c3m1.{ext}').read_bytes()
    print(f'PASS {tag:<7} | -O{opt} LTO={int(lto)} | {len(data):>6} B flash | SHA256 {digest}')
print('PASS: ARM vector sanity, LTO/non-LTO object types, read-only symbol contract, baseline reproducibility')

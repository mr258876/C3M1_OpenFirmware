#!/usr/bin/env python3
"""Host-side sanity checks for STM32F103 firmware binary and DNG."""
from pathlib import Path
import sys
import struct

def check_bin(path):
    b=Path(path).read_bytes()
    assert 1024 <= len(b) <= 0x80000, f"Firmware size unexpected: {len(b)}"
    sp, reset=struct.unpack_from('<II',b)
    assert 0x20000000 < sp <= 0x20010000 and sp%4 == 0, hex(sp)
    assert reset & 1 and 0x08000000 <= (reset&~1) < 0x08000000+len(b), hex(reset)
    print(f'ARM vector OK; bytes={len(b)}; SP=0x{sp:08x}; reset=0x{reset:08x}')

def check_dng(path):
    b=Path(path).read_bytes()
    assert len(b)==308672, f'DNG size unexpected: {len(b)}'
    assert b[:4]==b'II*\0' and struct.unpack_from('<I',b,4)[0]==8
    count=struct.unpack_from('<H',b,8)[0]
    entries={}
    for i in range(count):
        off=10+i*12
        tag,typ,n,raw=struct.unpack_from('<HHII',b,off)
        entries[tag]=(typ,n,raw)
    assert entries[256]==(4,1,642)
    assert entries[257]==(4,1,480)
    assert entries[258]==(3,1,8)
    assert entries[262]==(3,1,32803)
    assert entries[273]==(4,1,512)
    assert entries[279]==(4,1,308160)
    assert entries[274]==(3,1,3), 'DNG display orientation must be 180 degrees'
    assert entries[33422]==(1,4,0x00010102), 'RAW CFA must be BGGR'
    assert entries[50728][:2] == (5,3), 'White balance metadata missing'
    neutral_offset=entries[50728][2]
    neutral=[struct.unpack_from('<II',b,neutral_offset+8*i) for i in range(3)]
    assert neutral==[(104500,100000),(100000,100000),(53300,100000)], neutral
    assert entries[50714]==(3,1,1), 'BlackLevel must match dark-frame median'
    assert entries[50721][:2]==(10,9)
    assert entries[50964][:2]==(10,9), 'ForwardMatrix1 missing'
    assert entries[50778]==(3,1,23), 'D50 experimental profile illuminant missing'
    color=[struct.unpack_from('<ii',b,entries[50721][2]+8*i) for i in range(9)]
    assert color[0]==(216326,100000) and color[1]==(-77936,100000)
    forward=[struct.unpack_from('<ii',b,entries[50964][2]+8*i) for i in range(9)]
    assert forward[0]==(55316,100000) and forward[5]==(-1800,100000)
    print(f'DNG header OK; BGGR 642x480 RAW8, orientation 3, neutral {neutral}, experimental CM/FM, {len(b)} bytes')
    try:
        import tifffile
        with tifffile.TiffFile(path) as t:
            a=t.asarray()
            assert a.shape==(480,642) and str(a.dtype)=='uint8'
            print('tifffile image decode OK')
    except ImportError:
        print('tifffile not installed (optional check skipped)')

if __name__=='__main__':
    check_bin(sys.argv[1]);check_dng(sys.argv[2]);print('PASS')

#!/usr/bin/env python3
"""Validate v173 DNG profile against frozen exact M675 Soft values.

Usage: python3 tests/test_v173_dng_profile.py GENERATED.DNG [M675_Soft_REFERENCE.DNG]
This test is portable without the original user photo. If supplied, compare
full byte representations of tone curve and both matrices with that DNG.
"""
from pathlib import Path
import struct
import sys
import tifffile
assert len(sys.argv) in (2,3)
made=Path(sys.argv[1]); reference=Path(sys.argv[2]) if len(sys.argv)==3 else None
curve_words=(
    0x00000000,0x00000000,0x3dcccccd,0x3dcccccd,
    0x3e8f5c29,0x3e8f5c29,0x3ee66666,0x3ee56042,
    0x3f1eb852,0x3f1a1cac,0x3f400000,0x3f343958,
    0x3f5c28f6,0x3f4e5604,0x3f6e147b,0x3f5fbe77,
    0x3f800000,0x3f800000
)
# EXACT rational values of v0.1.72 and C3_00037_M675_Soft.DNG.
color_nums=(216326,-77936,-31623,-43403,135031,8264,-11638,23588,49632)
forward_nums=(55316,25386,15721,17495,84305,-1800,4656,-34114,111979)

def expected_rat(nums):
    return tuple(part for num in nums for part in (num,100000))

with tifffile.TiffFile(made) as m:
    p=m.pages[0]
    assert p.shape==(480,642) and p.dtype.name=='uint8'
    assert len(p.tags)==27 and len(set(tag.code for tag in p.tags.values()))==27
    assert p.tags[273].value==(512,)
    assert p.tags[279].value==(642*480,)
    assert made.stat().st_size==512+642*480+112
    assert p.tags[50936].value=='C3M1 Soft M675 v1'
    assert p.tags[50934].value==b'C3M1 Soft M675 v1\0'
    assert p.tags[50941].value==0
    assert p.tags[50708].value=='C3M1 OV7725 RAW8'
    assert p.tags[50778].value==23
    assert p.tags[50714].value==1
    assert p.tags[50717].value==255
    assert p.tags[50721].value == expected_rat(color_nums)
    assert p.tags[50964].value == expected_rat(forward_nums)
    data=made.read_bytes()
    actual_curve=data[p.tags[50940].valueoffset:p.tags[50940].valueoffset+72]
    assert actual_curve==struct.pack('<18I',*curve_words)
    assert len(p.tags[50940].value)==18
    assert p.tags[50728].value == (104500,100000,100000,100000,53300,100000)
    raw_end=512+642*480
    for tag in (50708,50934,50936,50940):
        assert p.tags[tag].valueoffset>=raw_end
    for tag in (50721,50728,50964):
        assert 338<=p.tags[tag].valueoffset<512
    assert p.tags[50708].valueoffset+18 <= made.stat().st_size
    image=p.asarray()
    for row in (0,1,79,239,479):
        for col in (0,1,2,320,640,641):
            assert int(image[row,col]) == ((row+col)&255)
    if reference:
        assert reference.is_file(),f'REFERENCE_MISSING: {reference}'
        with tifffile.TiffFile(reference) as r:
            q=r.pages[0]
            assert p.tags[50721].value==q.tags[50721].value
            assert p.tags[50964].value==q.tags[50964].value
            raw=reference.read_bytes()
            assert actual_curve==raw[q.tags[50940].valueoffset:q.tags[50940].valueoffset+72]
            assert p.tags[50941].value==q.tags[50941].value==0
print('PASS v173: 27 IFD tags, RAW geometry, exact M675 Soft curve & matrices, profile offsets and names' + ('; verified against original reference DNG' if reference else '; verified against frozen exact test vectors'))

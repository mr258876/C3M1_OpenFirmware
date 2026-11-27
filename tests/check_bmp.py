#!/usr/bin/env python3
"""Independently decode the BMP file emitted by production storage code."""
import sys
from pathlib import Path
from PIL import Image

p=Path(sys.argv[1]); data=p.read_bytes()
assert len(data)==54+320*240*3
assert data[0:2]==b'BM'
assert int.from_bytes(data[2:6],'little')==len(data)
assert int.from_bytes(data[22:26],'little',signed=True)==-240
with Image.open(p) as im:
    im.load()
    assert im.format=='BMP' and im.size==(320,240) and im.mode=='RGB'
    for x,y in ((0,0),(1,0),(319,0),(0,239),(319,239),(128,69)):
        k=y*320+x
        hi=(2*k*17+43)&255
        lo=((2*k+1)*17+43)&255
        pixel=(hi<<8)|lo
        b=pixel & 31
        g=(pixel>>5)&63
        r=(pixel>>11)&31
        expect=((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2))
        assert im.getpixel((x,y))==expect,(x,y,im.getpixel((x,y)),expect)
print('BMP parser: top-down 320x240 RGB, 24-bit, decoded sample pixels verified')

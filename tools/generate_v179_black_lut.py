#!/usr/bin/env python3
"""Reproduce the v179 per-CFA LUT, optionally verify the original 45 DNGs.
Usage: python3 tools/generate_v179_black_lut.py [--raw-zip 'Black Levels BLC.zip'] [--output PATH]
By default print generated C to stdout. No changes to RAW files.
"""
import argparse,csv,io,zipfile
from pathlib import Path
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--csv',type=Path,default=root/'v179_calibration_45.csv')
p.add_argument('--raw-zip',type=Path);p.add_argument('--output',type=Path)
a=p.parse_args(); rows=list(csv.DictReader(a.csv.open(encoding='utf-8-sig')))
channels=['B','Gb','Gr','R']; gains=[1,2,4,8,16]; speeds=[50,100,150,300,500,1000,2000,4000,8000]
assert len(rows)==45
for i,r in enumerate(rows):
    assert (int(r['sequence']),int(r['gain']),int(r['shutter']),r['file'])==(i+1,gains[i//9],speeds[i%9],f'C3_{i+1:05d}.DNG')
if a.raw_zip:
    import numpy as np,tifffile
    with zipfile.ZipFile(a.raw_zip) as z:
        assert sorted(z.namelist())==[r['file'] for r in rows]
        for r in rows:
            with tifffile.TiffFile(io.BytesIO(z.read(r['file']))) as t:
                page=t.pages[0]; image=page.asarray()
                assert image.shape==(480,642) and image.dtype==np.uint8 and page.tags[33422].value==b'\x02\x01\x01\x00'
                for c,(y,x) in zip(channels,[(0,0),(0,1),(1,0),(1,1)]):
                    mean=image[y::2,x::2].mean()
                    assert abs(mean-float(r[c+'_DN']))<0.00000051
                    assert int(np.floor(mean*1000+0.5))==int(r[c+'_mDN'])
header='''/* v179: generated from Black Levels BLC.zip (45 frames, one per setting).
 * Units 1/1000 DN, STORED BGGR order B,Gb,Gr,R. Dial 0..8 means
 * 1/8000,1/4000,1/2000,1/1000,1/500,1/300,1/150,1/100,1/50.
 * Experimental single-camera calibration; no RAW pixel subtraction. */
static const uint16_t black_lut[5][9][4] = {\n'''
for g in gains:
    header+=f'    {{ /* {g}x */\n'
    for r in reversed([r for r in rows if int(r['gain'])==g]):
        header+='        {'+', '.join(str(r[c+'_mDN'])+'u' for c in channels)+'}, /* 1/'+r['shutter']+' */\n'
    header+='    },\n'
header+='};\n'
if a.output:a.output.write_text(header)
else:print(header,end='')

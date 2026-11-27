#!/usr/bin/env python3
"""Validate actual C-generated metadata, source calibration and RAW bytes.
Usage: test_v179_black.py calibrated.bin legacy.bin calibration.csv [Black Levels BLC.zip]
The optional original ZIP lets us independently recompute all 180 means.
"""
from pathlib import Path
import sys,csv,io,struct,zipfile
import numpy as np
import tifffile
new,old,csvfile=map(Path,sys.argv[1:4]); raws=Path(sys.argv[4]) if len(sys.argv)>4 else None
rows=list(csv.DictReader(csvfile.open(encoding='utf-8-sig')))
assert len(rows)==45
channels=('B','Gb','Gr','R'); keys=[x+'_mDN' for x in channels]
values=np.array([[int(r[c]) for c in keys] for r in rows],dtype=np.int64).reshape(5,9,4)
# CSV order slow->fast; convert to dial order for model expectations.
L=values[:,::-1,:]; T=[1500,3000,6000,12000,24000,40000,80000,120000,240000]
def lerp(a,b,n,d): return (a*(d-n)+b*n+d//2)//d
def at(g,rows):
    if rows==0: return (L[g].sum(axis=0)+4)//9
    ticks=rows*784
    if ticks<=T[0]: return L[g,0]
    if ticks>=T[-1]: return L[g,-1]
    i=next(i for i in range(8) if ticks<=T[i+1]);return lerp(L[g,i],L[g,i+1],ticks-T[i],T[i+1]-T[i])
expected=list(values.reshape(45,4))+[lerp(L[0,4],L[1,4],1,2),lerp(L[1,4],L[2,4],1,2),L[4,4],at(2,1),at(2,10),at(2,8191),at(2,0),[1000]*4,L[3,4],at(3,10),at(1,10),at(2,10),L[1,4],[1000]*4,[1000]*4]
blob=new.read_bytes(); stride=512+168;assert len(blob)==len(expected)*stride
oldblob=old.read_bytes();assert len(oldblob)==512+112
synthetic=np.arange(480*642,dtype=np.uint8).reshape(480,642).tobytes()
def page(buf): return tifffile.TiffFile(io.BytesIO(buf))
with page(oldblob[:512]+synthetic+oldblob[512:]) as t:
    p=t.pages[0]; ref={tag.code:tag.value for tag in p.tags.values()};assert ref[50714]==1
    assert ref[273]==(512,) and ref[279]==(642*480,)
    assert len(ref)==27
archive=zipfile.ZipFile(raws) if raws else None
res=[]
try:
    for i,expect in enumerate(expected):
        frame=blob[i*stride:(i+1)*stride];raw=synthetic
        if archive and i<45:
            with page(archive.read(rows[i]['file'])) as t:
                p=t.pages[0];image=p.asarray();assert image.shape==(480,642) and image.dtype==np.uint8
                assert p.tags[33422].value==b'\x02\x01\x01\x00' and p.tags[50714].value==1
                calc=np.array([image[y::2,x::2].mean() for y,x in [(0,0),(0,1),(1,0),(1,1)]])
                assert np.array_equal(np.floor(calc*1000+0.5),expect),(i,calc,expect)
                raw=image.tobytes();res.extend(calc-np.asarray(expect)/1000)
        buf=frame[:512]+raw+frame[512:]
        with page(buf) as t:
            p=t.pages[0];tags={tag.code:tag.value for tag in p.tags.values()}
            assert len(tags)==28 and list(tags)==sorted(tags)
            assert tags[50713]==(2,2) and p.tags[50714].dtype==5 and p.tags[50714].count==4
            assert tags[50714]==tuple(v for x in expect for v in (int(x),1000)),(i,tags[50714],expect)
            for tag in ref:
                if tag==50714 or (i==len(expected)-1 and tag==50728):continue
                assert tags[tag]==ref[tag],(i,tag,tags[tag],ref[tag])
            if i==len(expected)-1: assert tags[50728]==(104500,100000,100000,100000,53300,100000)
            assert tags[273]==(512,) and tags[279]==(308160,)
            assert len(buf)==308840
            assert p.asarray().tobytes()==raw
            # Verify offsets, no entry overlap, no pointers into Bayer strip.
            areas=[]
            for tag in p.tags.values():
                if tag.valuebytecount>4:
                    a=tag.valueoffset;b=a+tag.valuebytecount
                    assert (350<=a<b<=512) or (308672<=a<b<=len(buf)),(tag.code,a,b)
                    areas.append((a,b,tag.code))
            for j,(a,b,tag) in enumerate(areas):
                for c,d,other in areas[:j]:
                    if (tag,other) in ((50936,50934),(50934,50936)):assert (a,b)==(c,d)
                    else: assert b<=c or d<=a,(tag,other,a,b,c,d)
finally:
    if archive: archive.close()
print(f'PASS v179: {len(expected)} DNG fixtures, 45 exact cells, fractional gain, auto timing, clamps, invalid readback, shot reset, all TIFF pointers, original CM/FM/WB/Soft, byte-exact Bayer')
if res: print(f'PASS original 45 BLC RAW files: 180 means reproduced, max calibration rounding residual {max(map(abs,res)):.6f} DN (in-sample only)')
# Source integration: capture hold -> metadata snapshot -> save, both paths.
root=Path(__file__).resolve().parents[1]; main=(root/'C3M1_Camera/Core/Src/main.c').read_text();store=(root/'C3M1_Camera/Core/Src/c3_storage.c').read_text()
assert main.count('c3_dng_capture_black_level(')==2
for block in main.split('} else if (ok && c3_camera_capture(1200u) && c3_camera_raw_hold()) {')[1:]:
    capture=block.split('c3_storage_save(pending_format)',1)[0];assert 'c3_dng_capture_black_level(' in capture and 'c3_recovery_captured(&recovery)' in capture
assert 'c3_dng_capture_black_level' not in store
assert 'c3_dng_make_tail(block)' in store and 'C3_DNG_TAIL_BYTES' in store
print('PASS v179 integration: both capture paths snapshot only held frames, no metadata recomputation on storage retry')

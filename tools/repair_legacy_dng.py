#!/usr/bin/env python3
"""Replace ONLY the 512-byte metadata prefix of legacy C3M1 v0.1.55 DNGs.

Usage: python tools/repair_legacy_dng.py old.DNG corrected.DNG
Works only on the exact known legacy RAW8 644x480 layout, deliberately rejects
unrelated DNGs. RAW bytes starting at offset 512 are copied verbatim.
"""
from pathlib import Path
import argparse
import hashlib
import struct

HEADER=512
WIDTH,HEIGHT=644,480
RAW=WIDTH*HEIGHT

def entries(src):
    if src[:4] != b"II*\0" or struct.unpack_from('<I',src,4)[0]!=8:
        raise ValueError('Not a little-endian TIFF/DNG with IFD offset 8')
    n=struct.unpack_from('<H',src,8)[0]
    if n>30 or 10+n*12+4>HEADER: raise ValueError('Unsupported IFD size')
    out={}
    for i in range(n):
        tag,kind,count,value=struct.unpack_from('<HHII',src,10+i*12)
        out[tag]=(kind,count,value)
    return out

def repair(inp:Path,outp:Path):
    if inp.resolve()==outp.resolve():
        raise ValueError('Input and output must be different files')
    dat=inp.read_bytes()
    if len(dat)!=HEADER+RAW: raise ValueError('Expected 309632-byte C3M1 RAW8 DNG')
    meta=entries(dat)
    required={256:(4,1,WIDTH),257:(4,1,HEIGHT),258:(3,1,8),
              273:(4,1,HEADER),279:(4,1,RAW),274:(3,1,1),
              33422:(1,4,0x02010100)}
    for tag,val in required.items():
        if meta.get(tag)!=val:
            raise ValueError(f'Legacy DNG tag {tag} does not match expected layout')
    h=(Path(__file__).resolve().parent/'DNG_Header_v156.bin').read_bytes()
    if len(h)!=HEADER or entries(h).get(274)!=(3,1,3):
        raise ValueError('DNG header template invalid')
    outp.write_bytes(h+dat[HEADER:])
    chk=outp.read_bytes()
    assert chk[HEADER:]==dat[HEADER:] and len(chk)==len(dat)
    print(f'Updated {outp} ({len(chk)} bytes); RAW SHA256: '
          f'{hashlib.sha256(chk[HEADER:]).hexdigest()}')

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('old',type=Path);ap.add_argument('corrected',type=Path)
    args=ap.parse_args()
    repair(args.old,args.corrected)

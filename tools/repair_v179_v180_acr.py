#!/usr/bin/env python3
"""Losslessly convert C3M1 v179/v180 RAW8 2x2 BlackLevel DNG to H or E.
Only accepts the exact 512+308160+168-byte firmware layout. Never overwrites.
Python 3 standard library only. Usage: python repair_v179_v180_acr.py FILE [FILE...] [--scalar]
"""
import argparse
from pathlib import Path
import struct

def convert(data,scalar=False):
    raw_start=512; n=642*480; tail_start=raw_start+n
    if len(data)!=tail_start+168 or data[:8]!=b'II*\0\x08\0\0\0':
        raise ValueError('not a complete v179/v180 512-byte-prefix calibrated DNG')
    h=bytearray(data[:512]); tail=bytearray(data[tail_start:]); raw=data[512:tail_start]
    count=struct.unpack_from('<H',h,8)[0]
    if count!=28 or struct.unpack_from('<I',h,10+12*count)[0]!=0:
        raise ValueError('unexpected IFD layout')
    entries={}
    for i in range(count):
        p=10+12*i; tag,typ,num,val=struct.unpack_from('<HHII',h,p)
        if tag in entries: raise ValueError('duplicate tag')
        entries[tag]=(typ,num,val,p)
    required={256:(4,1,642),257:(4,1,480),258:(3,1,8),259:(3,1,1),262:(3,1,32803),273:(4,1,512),274:(3,1,3),277:(3,1,1),278:(4,1,480),279:(4,1,n),284:(3,1,1),33421:(3,2,0x00020002),33422:(1,4,0x00010102),50708:(2,17,tail_start),50713:(3,2,0x00020002),50714:(5,4,tail_start+136),50717:(4,1,255),50721:(10,9,352),50728:(5,3,tail_start+112),50934:(1,18,tail_start+20),50936:(2,18,tail_start+20),50940:(11,18,tail_start+40),50964:(10,9,424)}
    for tag,want in required.items():
        if entries.get(tag,())[:3]!=want: raise ValueError(f'unexpected tag {tag}: {entries.get(tag)}')
    # Reject extra or malformed out-of-line fields; these files have no other metadata areas.
    expected=set(required)|{254,50706,50707,50778,50941}
    if set(entries)!=expected: raise ValueError('unexpected tags')
    blacks=[struct.unpack_from('<II',tail,136+8*i) for i in range(4)]
    if any(d!=1000 or v>=255000 for v,d in blacks): raise ValueError('invalid calibrated black level')
    if any(struct.unpack_from('<I',tail,116+i*8)[0]==0 for i in range(3)): raise ValueError('invalid neutral')
    def patch(tag,typ,num,val): struct.pack_into('<HHII',h,entries[tag][3],tag,typ,num,val)
    if scalar:
        patch(50713,3,2,0x00010001);patch(50714,5,1,tail_start+136)
        tail[136:168]=b'\0'*32;struct.pack_into('<II',tail,136,(sum(v for v,d in blacks)+2)//4,1000)
        return bytes(h)+raw+bytes(tail)
    patch(258,3,1,16);patch(279,4,1,2*n);patch(50717,4,1,65280)
    for tag in (50708,50714,50728,50934,50936,50940):
        typ,num,val,_=entries[tag];patch(tag,typ,num,val+n)
    expanded=bytearray(2*n);expanded[1::2]=raw
    for i,(v,d) in enumerate(blacks): struct.pack_into('<II',tail,136+8*i,v*256,d)
    return bytes(h)+bytes(expanded)+bytes(tail)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('files',type=Path,nargs='+');p.add_argument('--scalar',action='store_true',help='E: retain RAW8 and use scalar mean (loses per-channel correction)');a=p.parse_args();failed=False
    for src in a.files:
        dst=src.with_name(src.stem+('_ACR8' if a.scalar else '_ACR16')+'.DNG')
        try:
            out=convert(src.read_bytes(),a.scalar)
            with dst.open('xb') as f: f.write(out)
            print(f'{src} -> {dst} ({len(out)} bytes)')
        except (OSError,ValueError,struct.error) as e:
            print(f'SKIP {src}: {e}');failed=True
    return int(failed)
if __name__=='__main__': raise SystemExit(main())

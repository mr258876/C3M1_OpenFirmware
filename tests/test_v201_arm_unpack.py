#!/usr/bin/env python3
"""Interpret the actually linked Thumb unpack routine, not its C fallback.
Software instruction semantics only; no timing or bus arbitration model."""
from pathlib import Path
import re,random,struct,subprocess
root=Path(__file__).resolve().parent.parent
rng=random.Random(201)
for variant in ['ASM','ASM_PROFILE']:
    elf=root/f'build_V201_{variant}_ACR8_F2/c3m1.elf'
    asm=subprocess.check_output(['llvm-objdump','-d','--no-show-raw-insn','--disassemble-symbols=c3_unpack_asm',str(elf)],text=True)
    lines=[]
    for l in asm.splitlines():
        m=re.match(r'\s*([0-9a-f]+):\s+(\w+)\s+(.+)',l)
        if m: lines.append((int(m[1],16),m[2],m[3].split(' @')[0].strip()))
    assert len(lines)==22,asm
    index={a:i for i,(a,_,_) in enumerate(lines)}
    def reg(x):return int(x[1:])
    def imm(x):return int(x.lstrip('#'),0)
    def emulate(raw):
        r=[0,0,0,0,0xdead4,0xdead5,0xdead6,0xdead7]; saved=r[4:].copy(); src=struct.pack('<640H',*raw);out=bytearray(b'\xa5'*640); stack=[];ip=0;zero=False;steps=0;loads=stores=0
        while True:
            _,op,arg=lines[ip];ip+=1;steps+=1
            assert steps<2000
            a=arg.split(', ')
            if op=='push':assert arg=='{r4, r5, r6, r7}';stack=r[4:].copy()
            elif op=='pop':assert arg=='{r4, r5, r6, r7}';r[4:]=stack;stack=[]
            elif op=='movs':r[reg(a[0])]=imm(a[1]);zero=r[reg(a[0])]==0
            elif op=='ldm':
                assert arg=='r1!, {r2, r3, r4, r5}' and r[1]%4==0 and r[1]+16<=len(src)
                r[2:6]=struct.unpack_from('<4I',src,r[1]);r[1]+=16;loads+=4
            elif op=='lsrs':r[reg(a[0])]=r[reg(a[1])]>>imm(a[2]);zero=r[reg(a[0])]==0
            elif op=='bfi':
                d,s,pos,width=reg(a[0]),reg(a[1]),imm(a[2]),imm(a[3]);mask=((1<<width)-1)<<pos
                r[d]=((r[d]&~mask)|((r[s]<<pos)&mask))&0xffffffff
            elif op=='stm':
                assert arg=='r0!, {r2, r4}' and r[0]%4==0 and r[0]+8<=len(out)
                struct.pack_into('<2I',out,r[0],r[2],r[4]);r[0]+=8;stores+=2
            elif op=='subs':assert a[0]=='r7';r[7]=(r[7]-imm(a[1]))&0xffffffff;zero=r[7]==0
            elif op=='bne':
                if not zero:ip=index[int(a[0].split()[0],16)]
            elif op=='bx':assert arg=='lr';break
            else:raise AssertionError((op,arg))
        assert not stack and r[4:]==saved and r[0]==640 and r[1]==1280 and loads==320 and stores==160
        assert out==bytes((s>>1)&255 for s in raw)
        return steps
    steps=0;rows=0
    # Every possible GPIO halfword occurs; then mixed random data and guard extremes.
    for base in range(0,65536,640):
        steps=emulate([(base+i)&65535 for i in range(640)]);rows+=1
    for k in range(500):steps=emulate([rng.randrange(65536) for _ in range(640)]);rows+=1
    print(f'PASS {variant}: {rows} linked ARM rows, {rows*640} input halfwords; {steps} instructions/row including prologue and return, 320 word reads,160 word writes; AAPCS preserved.')

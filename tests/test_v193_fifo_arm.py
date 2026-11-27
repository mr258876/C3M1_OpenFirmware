#!/usr/bin/env python3
"""Interpret the *linked* low-reg row routines in a GPIO/SRAM model.
Not a Cortex-M3 cycle/electrical simulator. Decode the actual disassembly,
check all GPIO transactions, row bytes, canaries, and instruction spacing.
"""
import re, subprocess, random, sys
from pathlib import Path

def read_code(elf):
    dis=subprocess.check_output(['llvm-objdump','-d','--disassemble-symbols=c3_fifo_lowreg_row',str(elf)],text=True)
    result=[]
    for line in dis.splitlines():
        m=re.match(r'\s*([\da-f]+):\s+([\da-f]{4}(?: [\da-f]{4})?)\s+([a-z.]+)\s*(.*?)\s*$',line)
        if m: result.append((int(m[1],16),len(m[2].replace(' ',''))//2,m[3],m[4].split(' @ ')[0].strip()))
    assert result,elf
    return result,dis

def simulate(code, samples, rng, pack):
    # Callee-saved prologue/epilogue is compiler-generated; only model the
    # initialized loop registers, peripheral transactions and stores.
    regs={'r0':0x20001010}; memory=bytearray([0xa5]*672)
    idx={a:i for i,(a,_,_,_) in enumerate(code)}
    start=next(i for i,(_,_,op,args) in enumerate(code) if op=='str' and '#0x14]' in args)
    end=next(i for i,(_,_,op,args) in enumerate(code) if i>start and op=='bne')
    for _,_,op,args in code[:start]:
        if op in ('movw','movt','movs'):
            reg,n=args.split(', ');n=int(n[1:],0)
            regs[reg]=(regs.get(reg,0)&65535)|(n<<16) if op=='movt' else n
        else: assert op in ('push','add','str'),(op,args)
    assert regs['r0']==0x20001010
    assert code[end][3].startswith(hex(code[start][0]))
    i=start; steps=0; cursor=0; pending=False; state='high'; high_at=None; stores=0; z=False; low_at=None; widths=[]
    def memarg(args):
        m=re.fullmatch(r'(r\d+), \[(r\d+)(?:, #(0x[\da-f]+|\d+))?\]',args)
        assert m,args
        return m[1],regs[m[2]]+int(m[3] or '0',0)
    while i<=end:
        _,width,op,args=code[i];steps+=1; assert steps<6000
        if op=='str':
            assert width==2
            r,a=memarg(args);assert regs[r]==16
            if a==0x40010c14:
                assert state=='high' and not pending and cursor<640
                if high_at is not None: assert steps-high_at>=3
                state='low';low_at=steps
            elif a==0x40010c10:
                assert state=='low' and pending and steps-low_at==2
                pending=False;state='high';high_at=steps;cursor+=1
            else: raise AssertionError(hex(a))
        elif op=='ldr':
            assert width==2
            r,a=memarg(args);assert a==0x40012008 and state=='low' and not pending and steps==low_at+1
            regs[r]=(rng.getrandbits(32)&~0x1fe)|(samples[cursor]<<1);pending=True
        elif op in ('strb','strh'):
            assert width==2 and state=='high'
            r,a=memarg(args);n=2 if op=='strh' else 1
            assert n==(2 if pack else 1)
            k=a-0x20001000;assert 16<=k and k+n<=656
            assert k==16+stores*n
            memory[k:k+n]=(regs[r]&((1<<(8*n))-1)).to_bytes(n,'little');stores+=1
        elif op in ('ubfx','bfi','lsrs'):
            p=args.split(', ');dst,src=p[:2];a=int(p[2][1:],0)
            if op=='lsrs': regs[dst]=regs[src]>>a
            else:
                n=int(p[3][1:],0);m=(1<<n)-1
                regs[dst]=(regs[src]>>a)&m if op=='ubfx' else ((regs[dst]&~(m<<a))|((regs[src]&m)<<a))
        elif op in ('adds','subs'):
            r,a=args.split(', ');v=int(a[1:],0);regs[r]+=v if op=='adds' else -v;z=regs[r]==0
        elif op=='nop': pass
        elif op=='bne':
            if not z: i=start;continue
        else: raise AssertionError((op,args))
        i+=1
    assert state=='high' and not pending and cursor==640 and stores==(320 if pack else 640)
    assert memory[:16]==bytes([0xa5]*16) and memory[656:]==bytes([0xa5]*16)
    assert memory[16:656]==samples
    assert regs['r0']==0x20001010+640
    return steps, sum(x[1] for x in code[start:end+1])

if __name__=='__main__':
    base=Path(__file__).resolve().parent.parent
    rng=random.Random(193)
    for v in ('PACK16','ASM8','PROFILE_ASM8'):
        elf=base/f'build_V193_{v}_ACR8_F2/c3m1.elf'
        code,dis=read_code(elf)
        for i in range(64):
            data=bytes([[0,255,0x55,0xaa][i]])*640 if i<4 else rng.randbytes(640)
            steps,size=simulate(code,data,rng,v=='PACK16')
        print(f'PASS {v}: 64 rows, 640 ordered BRR/IDR/BSRR samples each; narrow GPIO instructions, >=2 intervening instructions in high phase, exact bytes/canaries, {steps} modeled loop instructions per row, {size} B loop body')

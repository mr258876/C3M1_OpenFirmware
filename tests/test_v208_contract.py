from pathlib import Path
import subprocess,tempfile,hashlib,struct
root=Path(__file__).resolve().parents[1]
core=root/'C3M1_Camera/Core';main=(core/'Src/main.c').read_text();body=main[main.index('int main(void)'):]
assert body.index('MX_GPIO_Init();')<body.index('cam_power_tick=HAL_GetTick()')<body.index('HAL_Delay(C3_LCD_POWER_SETTLE_MS);')<body.index('c3_lcd_init();')<body.index('c3_boot_remaining_ms(cam_power_tick,HAL_GetTick(),C3_CAM_POWER_SETTLE_MS)')<body.index('sccb_init();')<body.index('c3_camera_preview_init();')
assert '#define C3_CAM_POWER_SETTLE_MS 950u' in main
assert 'zoom_diag_failure(c3_camera_last_init_stage);' in main
assert 'zoom_diag_failure(c3_zoom_read_failure);' in main
assert 'HAL_Delay(2500u);' in main
code=r'''
#include <stdio.h>
#include <assert.h>
#include "c3_boot_timing.h"
int main(void){
 uint32_t starts[]={0,1,10000,UINT32_MAX-100,UINT32_MAX-1,UINT32_MAX};
 unsigned n=0;
 for(unsigned i=0;i<6;i++)for(uint32_t elapsed=0;elapsed<=5000;elapsed++){
   uint32_t now=starts[i]+elapsed;
   uint32_t a=c3_boot_remaining_ms(starts[i],now,950),b=c3_boot_remaining_ms(starts[i],now,1000);
   assert(a==(elapsed<950?950-elapsed:0));
   assert(b==(elapsed<1000?1000-elapsed:0));
   assert(a<=b && b-a<=50);
   assert(c3_boot_remaining_ms(starts[i],now+b,1000)==0);
   n++;
 }
 printf("PASS %u warmup deadline comparisons, including uint32 wrap, extra wait bounded by 50ms\n",n);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'boot.c';p.write_text(code);exe=p.with_suffix('')
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-I'+str(core/'Inc'),str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
for variant in ('CAM1000','CAM1000_DIAG','REF950'):
    out=root/f'build_V208_{variant}_ACR8_F2';image={};base=0;ended=False
    for line in (out/'c3m1.hex').read_text().splitlines():
        b=bytes.fromhex(line[1:]);assert line.startswith(':') and len(b)==b[0]+5 and sum(b)%256==0
        typ=b[3];addr=int.from_bytes(b[1:3],'big')
        if typ==4:base=int.from_bytes(b[4:6],'big')<<16
        elif typ==0:
            for i,v in enumerate(b[4:4+b[0]]):assert base+addr+i not in image;image[base+addr+i]=v
        elif typ==1:ended=True;break
        elif typ==5:pass
        else:raise AssertionError(typ)
    assert ended and min(image)==0x08000000 and max(image)<0x0807f000
    raw=bytes(image[x] for x in range(min(image),max(image)+1));assert raw==(out/'c3m1.bin').read_bytes()
    assert struct.unpack_from('<I',raw)[0]==0x20010000
    reset=struct.unpack_from('<I',raw,4)[0];assert reset&1 and 0x08000000<=(reset&~1)<0x08000000+len(raw)
    with tempfile.TemporaryDirectory() as d:
        p=Path(d)/'fromelf.bin';subprocess.run(['llvm-objcopy','-O','binary',str(out/'c3m1.elf'),str(p)],check=True);assert p.read_bytes()==raw
    syms=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
    for n in ('c3_storage_save','c3_camera_capture','live_raw','live_out','zoom_hold','c3_playback_enter'):
        assert n in syms,n
    assert ('c3_zoom_read_failure' in syms)==variant.endswith('_DIAG')
    assert (b'SNAP: WB RANGE\0' in raw)==variant.endswith('_DIAG')
    assert b'4X LOCK FAIL\0' in raw
    print(f'PASS {variant}: {len(raw)} B HEX/BIN/ELF exact, vector/settings limits, SHA256 {hashlib.sha256(raw).hexdigest()}')

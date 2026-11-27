#!/usr/bin/env python3
"""Boot deadline / diagnostic formatting host tests and production source guards.
Does not model board power rails, sensor startup or real SD response times.
"""
from pathlib import Path
import subprocess, tempfile, hashlib, struct
root=Path(__file__).resolve().parents[1]
core=root/'C3M1_Camera/Core'
m=(core/'Src/main.c').read_text()
g=(core/'Src/gpio.c').read_text()
l=(core/'Src/c3_lcd.c').read_text()
main=m[m.index('int main(void)'):]
assert main.index('MX_GPIO_Init();')<main.index('cam_power_tick=HAL_GetTick()')<main.index('HAL_Delay(C3_LCD_POWER_SETTLE_MS);')<main.index('c3_lcd_init();')<main.index('c3_lcd_set_rotation(settings.rotate_180);')<main.index('c3_lcd_backlight_on();')
assert main.index('c3_boot_remaining_ms(cam_power_tick,HAL_GetTick(),C3_CAM_POWER_SETTLE_MS)')<main.index('HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3, GPIO_PIN_SET);')<main.index('HAL_Delay(5u);')<main.index('sccb_init();')<main.index('(void)ov7725_write_reg(0x12u,0x80u);')<main.index('HAL_Delay(60u);')<main.index('c3_camera_preview_init();')
assert 'if(cam_wait)HAL_Delay(cam_wait);\n#else\n    HAL_Delay(C3_CAM_POWER_SETTLE_MS);' in main
assert main.index('c3_camera_preview_stop(); /* no capture while mounting SD */')<main.index('MX_SDIO_SD_Init();')<main.index('MX_FATFS_Init();')<main.index('c3_storage_mount();')<main.index('if (camera_ok) c3_camera_preview_rearm();')<main.index('c3_input_reset(read_buttons(), HAL_GetTick());')
assert main.index('boot_errors_before=c3_live_errors;')<main.index('c3_camera_preview_draw(&settings, preview_zoom4x, sd_ok,')<main.index('boot_errors_before==c3_live_errors')<main.index('boot_first_ms=HAL_GetTick()-boot_origin_ms;')<main.index('boot_show_timing();')
assert 'HAL_Delay(' not in m[m.index('static void boot_show_timing(void)'):m.index('static char c3_hex_digit')]
assert 'HAL_GPIO_WritePin(GPIOD, GPIO_PIN_3 | GPIO_PIN_6, GPIO_PIN_SET);' in g
assert 'HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_PIN_SET);' in g
assert 'HAL_Delay(20);' in l and 'cmd(0x01); HAL_Delay(140);' in l and 'cmd(0x11); HAL_Delay(120);' in l
func=m[m.index('static void boot_digits4'):m.index('\n#endif\nstatic char c3_hex_digit')]
code=r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "c3_boot_timing.h"
static uint32_t boot_lcd_ms,boot_cam_ms,boot_sd_ms,boot_first_ms;
static uint8_t boot_cam_ok,boot_sd_ok;
static char line1[40],line2[40];
static void c3_lcd_rect(int x,int y,int w,int h,uint16_t c){assert(x==5&&y==28&&w==144&&h==23&&c==0);}
static void c3_lcd_text(int x,int y,const char *s,uint16_t color,int scale){
    assert(x==8 && (y==30||y==41) && color==0xffe0 && scale==1);
    assert(strlen(s)*6<=141);strcpy(y==30?line1:line2,s);
}
'''+func+r'''
int main(void){
    uint32_t starts[]={0,1,10000,UINT32_MAX-100,UINT32_MAX-1,UINT32_MAX};
    unsigned n=0;
    for(unsigned i=0;i<6;i++)for(uint32_t e=0;e<5001;e++){
        uint32_t t=starts[i]+e;
        uint32_t wait=c3_boot_remaining_ms(starts[i],t,950);
        assert(wait==(e<950?950-e:0));
        assert(e+wait>=950);
        assert(c3_boot_remaining_ms(starts[i],t+wait,950)==0);
        n++;
    }
    boot_lcd_ms=1472;boot_cam_ms=620;boot_sd_ms=150;boot_first_ms=2342;
    boot_cam_ok=1;boot_sd_ok=0;boot_show_timing();
    assert(!strcmp(line1,"LCD1472 CAM0620 SD0150"));
    assert(!strcmp(line2,"FIRST2342 C1 S0"));
    boot_lcd_ms=9999;boot_cam_ms=10000;boot_sd_ms=0;boot_first_ms=UINT32_MAX;
    boot_cam_ok=0;boot_sd_ok=1;boot_show_timing();
    assert(!strcmp(line1,"LCD9999 CAM++++ SD0000"));
    assert(!strcmp(line2,"FIRST++++ C0 S1"));
    printf("PASS %u warmup deadline cases (including tick wrap), diagnostics and overflow formatting\n",n);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'test.c';p.write_text(code);out=p.with_suffix('')
    subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-I'+str(core/'Inc'),str(p),'-o',str(out)],check=True)
    subprocess.run([str(out)],check=True)
print('PASS boot order, original reset/SCCB/SD sequence, first complete render measurement')
for variant in ('BOOT','BOOT500','BOOT_PROFILE','BASE_PROFILE'):
    out=root/f'build_V206_{variant}_ACR8_F2'
    image={};base=0;ended=False
    for line in (out/'c3m1.hex').read_text().splitlines():
        b=bytes.fromhex(line[1:]);assert line.startswith(':') and len(b)==b[0]+5 and sum(b)%256==0
        typ=b[3];addr=int.from_bytes(b[1:3],'big')
        if typ==4:base=int.from_bytes(b[4:6],'big')<<16
        elif typ==0:
            for i,v in enumerate(b[4:4+b[0]]):assert base+addr+i not in image;image[base+addr+i]=v
        elif typ==1:ended=True;break
        elif typ==5:pass
        else:raise AssertionError(typ)
    assert ended and min(image)==0x08000000
    raw=bytes(image[x] for x in range(min(image),max(image)+1))
    assert raw==(out/'c3m1.bin').read_bytes()
    assert struct.unpack_from('<I',raw)[0]==0x20010000
    reset=struct.unpack_from('<I',raw,4)[0]
    assert reset&1 and 0x08000000<=(reset&~1)<0x08000000+len(raw)
    with tempfile.TemporaryDirectory() as d:
        p=Path(d)/'fromelf.bin';subprocess.run(['llvm-objcopy','-O','binary',str(out/'c3m1.elf'),str(p)],check=True);assert p.read_bytes()==raw
    syms=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
    for name in ('zoom_hold','c3_zoom_hold_apply','c3_camera_capture','f_write','live_raw','live_out'):
        assert name in syms,name
    assert b'4X LOCK FAIL\0' in raw
    assert ('boot_first_seen' in syms)==variant.endswith('PROFILE')
    print(f'PASS {variant}: {len(raw)} B HEX/BIN/ELF match; SHA256 {hashlib.sha256(raw).hexdigest()}')

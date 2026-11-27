#!/usr/bin/env python3
"""Source and table-coverage contracts for v166 experimental fast RAW switch."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
main=(root/'C3M1_Camera/Core/Src/main.c').read_text()
cam=(root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
ov=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text(encoding='latin1')
config=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725cfg.h').read_text(encoding='latin1')
build=(root/'build_clang.sh').read_text()
assert 'C3_FAST_PREVIEW_CONTROL:1' in build
assert 'C3_FAST_RAW_SWITCH:0' in build
quick=main.split('static void resume_preview_controls(',1)[1].split('#if !C3_PREVIEW_PROBE',1)[0]
assert 'c3_camera_preview_stop();' in quick
assert 'c3_exposure_apply(' in quick
assert quick.index('c3_camera_preview_stop();') < quick.index('c3_exposure_apply(') < quick.index('c3_camera_preview_rearm();')
assert 'resume_preview(camera_ok,fps,exposure,meter);' in quick
assert 'c3_camera_preview_init(' not in quick
assert 'resume_preview_controls(&camera_ok,&fps,&exposure,&meter);' in main
assert 'c3_camera_preview_select_zoom(preview_zoom4x);\n            resume_preview(' in main
assert 'resume_preview(camera_ok,fps,exposure,meter);' in main.split('static void c3_run_sleep(',1)[1]
assert 'c3_camera_raw_fast_init()' in main
assert 'ov7725_RAW_fast_init();' in cam
assert 'code = ov7725_RAW_init();' in cam.split('uint8_t c3_camera_raw_fast_init(void)',1)[1]
segment=cam.split('uint8_t c3_camera_raw_fast_init(void)',1)[1].split('#endif',1)[0]
assert segment.index('code = ov7725_RAW_init();') > segment.index('if (code != 0u)')
assert 'C3_SCHED_STILL' in cam.split('static uint8_t still_rearm_after_sensor_init',1)[1]
assert 'ov7725_write_reg(COM3, 0x10u)' in ov
assert 'ov7725_write_reg(COM6, 0x42u)' in ov
assert ov.index('ov7725_write_reg(COM6, 0x42u)') < ov.index('ov7725_init_reg_tbRAW[i][0]',ov.index('uint8_t ov7725_RAW_fast_init(void)'))
for marker in ['verify_static_reg(COM7, 0x13u, 0xFFu)','verify_static_reg(COM3, 0x10u, 0xF8u)',
               'verify_static_reg(HSIZE, 0xA1u, 0xFFu)','verify_static_reg(VSIZE, 0xF0u, 0xFFu)',
               'verify_static_reg(MTX1, 0xB0u, 0xFFu)','verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)']:
    assert marker in ov, marker
# Prove every skipped pair is invariant in the actual RGB preview table.
def defs(data):
    return {k:int(v,16) for k,v in re.findall(r'#define\s+(\w+)\s+(0x[0-9A-Fa-f]+)',data)}
regnames=defs(config)
def table(name):
    text=config.split('const uint8_t '+name+'[][2] =',1)[1].split('};',1)[0]
    text=re.sub(r'/\*.*?\*/','',text,flags=re.S)
    text=re.sub(r'//[^\n]*','',text)
    out=[]
    for k,v in re.findall(r'\{\s*(\w+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}',text):
        out.append((int(k,16) if k.lower().startswith('0x') else regnames[k], int(v,16)))
    return out
rgb=dict(table('ov7725_init_reg_tb1'))
raw=table('ov7725_init_reg_tbRAW')
rgb[regnames['COM4']]=0x41
rgb[regnames['COM3']]=0xD0
skips={}
func=ov.split('static uint8_t raw_same_as_preview',1)[1].split('uint8_t ov7725_RAW_fast_init(void)',1)[0]
for c,val in re.findall(r'((?:case\s+\w+:\s*)+)return\s+\(uint8_t\)\(val\s*==\s*(0x[\da-fA-F]+)u\);',func):
    for name in re.findall(r'case\s+(\w+):',c):
        skips[regnames[name]]=int(val,16)
assert len(skips)>=34,len(skips)
for addr,v in skips.items():
    assert rgb.get(addr)==v, (addr, v, rgb.get(addr))
newwrites=[(addr,0 if addr==regnames['CLKRC'] else val) for addr,val in raw
           if skips.get(addr)!=(0 if addr==regnames['CLKRC'] else val)]
assert 18<=len(newwrites)<40,len(newwrites)
assert any(a==regnames['COM7'] for a,_ in newwrites)
assert any(a==regnames['DSP_Ctrl4'] for a,_ in newwrites)
assert any(a==regnames['HSIZE'] for a,_ in newwrites)
assert (regnames['COM3'],0x10) not in raw # explicit additional write required
print('PASS v166: preview controls rearm; RAW fast diff %d/%d writes (%d known equal), strict fallback' % (len(newwrites),len(raw),len(skips)))

assert 'C3_SAVED_HOLD_MS:100' not in build  # configured independently
assert 'C3_SAVED_HOLD_MS:-100' in build
assert 'HAL_Delay(C3_SAVED_HOLD_MS)' in main
assert 'HAL_Delay(230u)' not in main

#!/usr/bin/env python3
"""Static/source-level invariants for experimental OV7725 no-reset round trip.

Does NOT replace a camera test: detects unexpected table divergence, accidental
COM7 software reset in the fast path, missing pending-frame handling, and the
reinstatement order of AWB + exposure + preview FIFO.
"""
from pathlib import Path
import re
root=Path(__file__).resolve().parent.parent
src=root/'C3M1_Camera'
ov=(src/'Drivers/BSP/OV7725/ov7725.c').read_text(encoding='utf-8',errors='surrogateescape')
cfg=(src/'Drivers/BSP/OV7725/ov7725cfg.h').read_text(encoding='latin1')
main=(src/'Core/Src/main.c').read_text()
cam=(src/'Core/Src/c3_camera.c').read_text()
awb=(src/'Core/Src/c3_dng.c').read_text()
script=(root/'build_clang.sh').read_text()

def func(source,name):
    start=source.index('uint8_t '+name+'(')
    brace=source.index('{',start)
    depth=0
    for i in range(brace,len(source)):
        if source[i]=='{':depth+=1
        elif source[i]=='}':
            depth-=1
            if depth==0:return source[start:i+1]
    assert False,name

raw_to_rgb=func(ov,'ov7725_RGB_fast_restore')
rgb_to_raw=func(ov,'ov7725_RAW_fast_init')
assert '0x80u' not in raw_to_rgb and '0x80)' not in raw_to_rgb
assert '0x80u' not in rgb_to_raw and '0x80)' not in rgb_to_raw
assert 'ov7725_write_reg(COM8,0xF8u)' in raw_to_rgb
assert 'reg == COM8 || reg == COM4' in raw_to_rgb
assert 'ov7725_write_reg(COM3,(uint8_t)C3_CAM_COM3_RGB)' in raw_to_rgb
assert 'verify_static_reg(COM7,0x06u' in raw_to_rgb
assert 'verify_static_reg(COM3,(uint8_t)C3_CAM_COM3_RGB,0x78u)' in raw_to_rgb
assert 'verify_static_reg(DSP_Ctrl4,0x00u' in raw_to_rgb
assert 'verify_static_reg(CLKRC,0x00u' in raw_to_rgb
assert 'verify_static_reg(COM4,0x41u' in raw_to_rgb
assert 'C3_FAST_RGB_RESTORE:-0' in script
assert 'C3_RAW_LOCK_EXPOSURE' in script

# Reconcile a full RGB initialization table to a full RAW table (steady state).
# The differential restore must rewrite any unequal entries, plus dynamic
# control registers that are supplied by the runtime (AEC/AGC/AWB).
def parse(table):
    span=cfg.split('const uint8_t '+table+'[][2] =',1)[1]
    span=span.split('\n};',1)[0]
    span=re.sub(r'/\*.*?\*/','',span,flags=re.S)
    span=re.sub(r'//[^\n]*','',span)
    pairs=re.findall(r'\{\s*([^,{}]+?)\s*,\s*([^,{}]+?)\s*\}',span)
    return [(a.strip(),b.strip().lower()) for a,b in pairs]
raw=parse('ov7725_init_reg_tbRAW')
rgb=parse('ov7725_init_reg_tb1')
rm={k:v for k,v in raw}
critical={'COM7','COM8','COM4','COM6','CLKRC','HSTART','HSIZE','VSTRT',
          'VSIZE','HOutSize','VOutSize','EXHCL','TGT_B','AEW','AEB','VPT',
          'FixGain','AWB_Ctrl0','DSP_Ctrl1','DSP_Ctrl4','COM9','COM5'}
changed=[(k,v) for k,v in rgb if k in critical or rm.get(k)!=v]
assert len(raw)>=69 and len(rgb)>=60
assert 15 <= len(changed) <= 35,len(changed)
# RTC/mode-dependent values must never be assumed equal.
for reg in critical:
    assert f'case {reg}:' in ov

# Prove photo still preserved through save retries; only after commit/discard.
assert main.count('resume_preview_after_raw(&camera_ok,&fps,&exposure,&meter,')==3
assert main.index('if (c3_recovery_can_retry(&recovery))') < main.index('raw_noreset_pending=0u;',main.index('if (c3_recovery_can_retry(&recovery))'))
fast_resume=main.split('static void resume_preview_after_raw',1)[1].split('\n#endif',1)[0]
assert fast_resume.index('c3_camera_rgb_fast_restore()')<fast_resume.index('c3_exposure_restore_preview_snapshot(')
assert fast_resume.index('c3_dng_get_preview_awb_gains(')<fast_resume.index('c3_exposure_apply(')
assert fast_resume.index('c3_exposure_apply(')<fast_resume.index('c3_camera_preview_rearm();')
assert 'c3_camera_raw_fast_path_used' in main
assert 'c3_dng_get_preview_awb_gains' in awb
assert 'captured_sensor_awb' in awb
assert 'c3_camera_raw_fast_fallbacks' in cam
print(f'PASS v167: no-reset bidirectional path; {len(changed)} conservative RGB differences from {len(rgb)} table writes; AWB/AEC snapshot, pending retry and fallback')

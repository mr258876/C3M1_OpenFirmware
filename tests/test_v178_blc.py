#!/usr/bin/env python3
"""Compile and execute actual OV7725 mode functions against a register bus mock.

This tests table write order, masks, and failure returns, NOT analog convergence.
Both BLC builds use the real same cfg table and same init/restore functions.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
base = root/'C3M1_Camera'
ov = (base/'Drivers/BSP/OV7725/ov7725.c').read_text()
cfg = (base/'Drivers/BSP/OV7725/ov7725cfg.h').read_text(encoding='latin1')
main = (base/'Core/Src/main.c').read_text()
cam = (base/'Core/Src/c3_camera.c').read_text()

def take(name):
    m = re.search(r'(?:static\s+)?uint8_t\s+'+name+r'\s*\(', ov)
    assert m, name
    start = ov.index('{', m.end())
    depth = 0
    for i in range(start, len(ov)):
        if ov[i] == '{': depth += 1
        elif ov[i] == '}':
            depth -= 1
            if not depth: return ov[m.start():i+1]
    raise AssertionError(name)

code = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define C3_RGB_EXHCL 0x80u
#define C3_RAW_CLKRC 0u
#define C3_RAW_FREEZE_AWB 1
#define C3_RGB_PLL4_DEFAULT 1
#define C3_CAM_COM3_RGB 0xD0u
#define C3_CAM_COLOR_BAR 0
#define C3_RGB_VERIFY_WARN_ONLY 1
#define C3_SCCB_RESTART_VERIFY 1
#define C3_SHUTTER_DYNAMIC_TIMING 1
#define OV7725_PID 0x7721u
#define OV7725_MID 0x7FA2u
#define OV7725_MID1 0x7FFFu
#define OV7725_WEN(x) ((void)(x))
#define OV7725_WRST(x) ((void)(x))
#define OV7725_RRST(x) ((void)(x))
#define OV7725_OE(x) ((void)(x))
#define OV7725_RCLK(x) ((void)(x))
#define C3_OV_WARN_MTX1 1u
#define C3_OV_WARN_MTX2 2u
#define C3_OV_WARN_DSP4 4u
'''
code += cfg.replace('#include "ov7725.h"','')
code += r'''
volatile uint32_t c3_ov_last_mid,c3_ov_last_pid,c3_ov_last_bad_reg,c3_ov_last_bad_phase,
  c3_ov_last_verify_expected,c3_ov_last_verify_actual,c3_ov_last_dc_reg,c3_ov_last_dc_bits,
  c3_ov_verify_warn_mask,c3_ov_warn_first_reg,c3_ov_warn_first_expected,c3_ov_warn_first_actual,
  c3_sccb_restart_attempts,c3_sccb_restart_successes,c3_sccb_restart_last_reg;
volatile uint32_t c3_ov_blc_rgb_readback,c3_ov_blc_raw_init_readback,
  c3_ov_blc_raw_capture_readback,c3_ov_blc_raw_gain_readback;
static uint8_t blc_last_readback;
static uint8_t regs[256];
static unsigned writes[256], resets;
static int blocked_write = -1, forced_read = -1;
static uint8_t forced_value = 0xFFu, alternate_ok;
static void sccb_init(void) {}
static void delay_ms(unsigned x) {(void)x;}
static void reset_sensor(void) {
  memset(regs,0,sizeof(regs));
  regs[MIDH]=0x7F; regs[MIDL]=0xA2; regs[PID]=0x77; regs[VER]=0x21;
  regs[COM3]=0x10; regs[COM13]=0xE2;
  regs[TGT_B]=regs[TGT_R]=regs[TGT_Gb]=regs[TGT_Gr]=0x80;
  resets++;
}
static uint8_t ov7725_write_reg(uint8_t reg,uint8_t value) {
  writes[reg]++;
  if (reg==blocked_write) return 0; /* SCCB transaction completes, write lost */
  if (reg==COM7 && value==0x80) reset_sensor();
  else regs[reg]=value;
  return 0;
}
static uint8_t ov7725_read_reg(uint16_t r) {return r==forced_read?forced_value:regs[r];}
static uint8_t ov7725_read_reg_restart(uint8_t r) {return alternate_ok?regs[r]:ov7725_read_reg(r);}
'''
for name in ('verify_static_reg','verify_blc_config','ov7725_verify_raw_blc_capture',
             'rgb_verify_or_warn_if_ff','ov7725_init','ov7725_RAW_init','raw_same_as_preview',
             'ov7725_RAW_fast_init','rgb_matches_raw_static','ov7725_RGB_fast_restore'):
    code += '\n'+take(name)+'\n'
code += r'''
static void check_mode(uint8_t raw) {
  assert(regs[COM13]==(raw?C3_RAW_COM13:C3_RGB_COM13));
  assert(regs[TGT_B]==(raw?0x80:0x7F));
  assert(regs[TGT_R]==0x80 && regs[TGT_Gb]==0x80 && regs[TGT_Gr]==0x80);
  if(raw) assert(!(regs[COM5]&0x80));
}
int main(void) {
  assert(ov7725_init()==0); check_mode(0);
  assert(c3_ov_blc_rgb_readback==0xE2);
  unsigned r=resets;
  for (unsigned i=0;i<3;i++) {
    /* Deliberately corrupt RGB calibration state before switching. */
    regs[COM13]=0x00; regs[TGT_B]=0x7F; regs[TGT_R]=0x81;
    regs[TGT_Gb]=0x82; regs[TGT_Gr]=0x83;
    unsigned count=writes[COM13];
    assert(ov7725_RAW_fast_init()==0); check_mode(1);
    assert(writes[COM13]>count && resets==r);
    assert(c3_ov_blc_raw_init_readback==C3_RAW_COM13);
    regs[GAIN]=0x70;
    assert(ov7725_verify_raw_blc_capture());
    assert(c3_ov_blc_raw_capture_readback==C3_RAW_COM13);
    assert(c3_ov_blc_raw_gain_readback==0x70);
    /* Every RGB restore must write all targets, even shared values. */
    unsigned gr=writes[TGT_Gr]; count=writes[COM13];
    regs[TGT_Gr]=0x90;
    assert(ov7725_RGB_fast_restore()==0); check_mode(0);
    assert(writes[COM13]>count && writes[TGT_Gr]>gr && resets==r);
    assert(c3_ov_blc_rgb_readback==0xE2);
  }
  /* Full-reset RAW must have same BLC config as fast RAW. */
  assert(ov7725_RAW_init()==0); check_mode(1); assert(resets==r+1);
  /* Defined COM13 bits match despite reserved bits being different. */
  forced_read=COM13; forced_value=(uint8_t)(C3_RAW_COM13 ^ 0x1B);
  assert(ov7725_verify_raw_blc_capture());
  /* FF never accepted for COM13, any target, or night-mode gate. */
  uint8_t critical[]={COM13,TGT_B,TGT_R,TGT_Gb,TGT_Gr,COM5};
  forced_value=0xFF;
  for(unsigned i=0;i<sizeof(critical);i++) {
    forced_read=critical[i];
    assert(!ov7725_verify_raw_blc_capture());
    assert(c3_ov_last_bad_reg==critical[i]);
  }
  forced_read=COM13; alternate_ok=1;
  assert(ov7725_verify_raw_blc_capture());
  assert(c3_ov_blc_raw_capture_readback==C3_RAW_COM13);
  alternate_ok=0;
  assert(ov7725_RAW_fast_init()==4);
  assert(ov7725_RAW_init()==4);
  assert(ov7725_RGB_fast_restore()==4);
  assert(ov7725_init()==4);
  forced_read=-1;
  /* Lost target write must fail closed (no SCCB ACK assumption). */
  assert(ov7725_init()==0);
  blocked_write=TGT_B;
  assert(ov7725_RAW_fast_init()==4 && c3_ov_last_bad_reg==TGT_B);
  blocked_write=-1;
  assert(ov7725_RAW_init()==0); check_mode(1);
  blocked_write=TGT_B;
  assert(ov7725_RGB_fast_restore()==4 && c3_ov_last_bad_reg==TGT_B);
  blocked_write=-1;
  assert(ov7725_init()==0); check_mode(0);
  printf("PASS v178 BLC=%d: cold/full/fast init, three round trips, readback/FF/restart, late capture gate, lost writes\n", C3_RAW_BLC_ENABLE);
  return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp)/'test.c'; exe=Path(temp)/'test'
    path.write_text(code)
    for enable in (0,1):
        subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',f'-DC3_RAW_BLC_ENABLE={enable}',str(path),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

# Integration: no save call before the post-settle strict gate; both legacy and locked paths.
assert main.count('!ov7725_verify_raw_blc_capture()')==2
for m in re.finditer(r'!ov7725_verify_raw_blc_capture\(\)',main):
    s=main[m.start():main.index('c3_storage_save(pending_format)',m.end())]
    assert 'show_capture_init_error(pending_format, 0u)' in s
    assert '} else if (ok && c3_camera_capture(1200u) && c3_camera_raw_hold())' in s
assert main.count('settle < C3_DNG_SETTLE_LOCKED')==2
assert 'code = ov7725_RAW_init();' in cam.split('uint8_t c3_camera_raw_fast_init(void)',1)[1]
assert 'if (raw_was_fast && c3_camera_rgb_fast_restore())' in main
assert 'C3_DNG_SETTLE_LOCKED="${C3_DNG_SETTLE_LOCKED:-6}"' in (root/'build_v178.sh').read_text()
print('PASS v178 integration: settling and fail-closed before saved RAW, existing reset fallback preserved')

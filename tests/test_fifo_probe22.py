#!/usr/bin/env python3
"""Compile actual probe-capture state machine with a mocked VSYNC producer.
   This tests intended GPIO event ordering, not physical FIFO correctness.
"""
import re,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'C3M1_Camera/Core/Src/c3_fifo_probe.c').read_text()
def extract(name):
    m=re.search(r'static uint8_t\s+'+name+r'\s*\(',s)
    assert m is not None,name
    p=s.index('{',m.end());depth=0
    for end in range(p,len(s)):
        if s[end]=='{':depth+=1
        if s[end]=='}':
            depth-=1
            if depth==0:return s[m.start():end+1]
    raise AssertionError(name)
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static unsigned vsyncs, reset_active_during_write, transitions;
static unsigned wrst, wen, fail_at;
static void c3_camera_preview_stop(void){wen=0;}
static void gpio_write_enable(uint8_t on){
  wen=on;
  transitions++;
}
static void gpio_write_reset(uint8_t release){wrst=release;transitions++;}
static void HAL_Delay(uint32_t ms){(void)ms;}
static uint8_t next_edge(uint32_t ms){
  (void)ms;
  if (vsyncs++ == fail_at) return 0u;
  if (wen && !wrst) reset_active_during_write=1;
  /* VSYNC ISR forces WEN off while scheduler OFF. */
  wen=0;
  return 1u;
}
'''+extract('capture_frame')+r'''
int main(void){
  fail_at=99; vsyncs=0; wrst=1; wen=0;
  assert(capture_frame(1u)==1u);
  assert(reset_active_during_write==1u);
  assert(wen==0u && wrst==1u && vsyncs==4u);
  fail_at=99; vsyncs=0; wrst=1; wen=0;
  reset_active_during_write=0;
  assert(capture_frame(0u)==1u);
  assert(reset_active_during_write==0u);
  assert(wen==0u && wrst==1u && vsyncs==2u);
  fail_at=1; vsyncs=0; wrst=1; wen=0;
  assert(capture_frame(1u)==0u);
  assert(wen==0u && wrst==1u);
  puts("PASS: PRIME active-write reset, legacy inactive reset, timeout safety");
  return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'probe.c'; exe=Path(d)/'probe'
    p.write_text(code)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

# Ensure the production-only operations are absent from the actual ELF.
nm=subprocess.run(['nm',str(root/'build_probe/c3m1.elf')],capture_output=True,text=True,check=True).stdout
symbols={line.split()[-1] for line in nm.splitlines() if len(line.split())>=3}
for forbidden in ('f_mount','f_write','f_sync','c3_storage_save','c3_settings_flash_save','c3_camera_raw_init','c3_camera_bmp_init'):
    assert forbidden not in symbols, f'Unsafe {forbidden} present in probe'
assert 'c3_fifo_probe_run' in symbols
assert 'c3_camera_preview_draw' not in symbols
assert 'volatile uint32_t c3_sccb_half_period_us = 50u' in (root/'C3M1_Camera/Drivers/BSP/OV7725/sccb.c').read_bytes().decode('latin-1')
assert 'COM3,0x51u' in s and 'COM3,colorbar?0x51u:0x50u' in s
assert '/* actual data updates after RCK rising */' in s
print('PASS: test-only ELF, SCCB default 50us, sensor colorbar and post-RCK sample present')

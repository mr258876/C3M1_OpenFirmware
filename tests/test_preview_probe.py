#!/usr/bin/env python3
"""Exercise v0.1.20's actual warning/VSYNC C routines with a fake bus.
Tests software policy only. No actual camera/FSMC timing can be simulated.
"""
from pathlib import Path
import subprocess
import tempfile
import re

root = Path(__file__).resolve().parents[1]
a = (root / 'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_bytes().decode('latin-1')
b = (root / 'C3M1_Camera/Core/Src/c3_camera.c').read_text()


def take(source, name):
    match = re.search(r'(?:static\s+)?(?:void|uint8_t)\s+' + name + r'\s*\(', source)
    assert match is not None, name
    brace = source.index('{', match.start())
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == '{':
            depth += 1
        if source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[match.start():i + 1]
    raise AssertionError(name)


head = r'''
#include <assert.h>
#include <stdint.h>
/* v178 verify_static_reg also records the actual COM13 byte. */
#define COM13 0x3Eu
static uint8_t blc_last_readback;
#include <stdio.h>
#define OV7725_PID 0x7721u
#define C3_OV_WARN_MTX1 1u
#define C3_OV_WARN_MTX2 2u
#define C3_OV_WARN_DSP4 4u
#define C3_OV_WARN_PID_POST 8u
volatile uint32_t c3_ov_last_bad_reg, c3_ov_last_bad_phase;
volatile uint32_t c3_ov_last_verify_expected, c3_ov_last_verify_actual;
volatile uint32_t c3_ov_verify_warn_mask;
volatile uint32_t c3_ov_warn_first_reg, c3_ov_warn_first_expected, c3_ov_warn_first_actual;
volatile uint32_t c3_camera_pid_good_of_5, c3_camera_pid_last;
volatile uint32_t c3_camera_vsync_edges, c3_camera_vsync_last_ms;
static uint8_t registers[256];
static uint16_t pid_fake[5];
static unsigned pid_idx, pid_fake_active;
static uint32_t fake_tick, first_at, second_at;
static void delay_ms(uint32_t ms){ (void)ms; }
static uint32_t HAL_GetTick(void){ return fake_tick; }
static void HAL_Delay(uint32_t ms){
    for(uint32_t i=0;i<ms;i++){
        fake_tick++;
        if(fake_tick==first_at || fake_tick==second_at){
            c3_camera_vsync_last_ms=fake_tick; c3_camera_vsync_edges++;
        }
    }
}
static uint8_t ov7725_read_reg(uint16_t addr) {
    if (pid_fake_active && (addr==0x0Au || addr==0x0Bu)) {
        uint16_t value=pid_fake[pid_idx];
        uint8_t res=addr==0x0Au?(uint8_t)(value>>8):(uint8_t)value;
        if(addr==0x0Bu && pid_idx<4)pid_idx++;
        return res;
    }
    return registers[addr&0xFFu];
}
'''
source = head + '\n'.join(take(a, n) for n in ['verify_static_reg', 'preview_verify_warn'])
source += '\n'.join(take(b, n) for n in ['post_init_pid_ok', 'wait_for_vsync'])
source += r'''
int main(void){
    /* A readback mismatch is recorded as warning; first failure survives. */
    c3_ov_warn_first_reg=0xFFu;
    registers[0x94]=0xFFu; registers[0x95]=0x9Du; registers[0x67]=0x03u;
    preview_verify_warn(0x94u,0xB0u,0xFFu,C3_OV_WARN_MTX1);
    assert(c3_ov_verify_warn_mask == C3_OV_WARN_MTX1);
    assert(c3_ov_warn_first_reg == 0x94u);
    assert(c3_ov_warn_first_expected == 0xB0u);
    assert(c3_ov_warn_first_actual == 0xFFu);
    preview_verify_warn(0x95u,0x9Du,0xFFu,C3_OV_WARN_MTX2);
    assert(c3_ov_verify_warn_mask == C3_OV_WARN_MTX1);
    preview_verify_warn(0x67u,0x00u,0x03u,C3_OV_WARN_DSP4);
    assert(c3_ov_verify_warn_mask == (C3_OV_WARN_MTX1|C3_OV_WARN_DSP4));
    assert(c3_ov_warn_first_reg == 0x94u);

    pid_fake_active=1;
    pid_fake[0]=0x7721; pid_fake[1]=0x7F5F;
    pid_fake[2]=0x7721; pid_fake[3]=0xFFFF; pid_fake[4]=0x7721;
    assert(post_init_pid_ok()==1u && c3_camera_pid_good_of_5==3u);
    pid_idx=0;
    pid_fake[0]=0xFFFF; pid_fake[2]=0x7F5F; pid_fake[4]=0xFFFF;
    assert(post_init_pid_ok()==0u && c3_camera_pid_good_of_5==0u);

    fake_tick=0; first_at=7; second_at=70;
    c3_camera_vsync_edges=0;
    assert(wait_for_vsync(300u)==1u);
    fake_tick=0; first_at=0; second_at=0;
    c3_camera_vsync_edges=0;
    assert(wait_for_vsync(100u)==0u);
    fake_tick=0; first_at=7; second_at=10;
    c3_camera_vsync_edges=0;
    assert(wait_for_vsync(120u)==0u); /* <=5ms glitch is insufficient */
    puts("PASS: warning capture, post-PID telemetry, VSYNC gate success/timeout/jitter");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / 'test.c'
    exe = Path(d) / 'test'
    path.write_text(source)
    subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(path), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The shipping diagnostic default must omit storage writes at the linker level.
m = (root / 'C3M1_Camera/Core/Src/main.c').read_text()
assert '#define C3_PREVIEW_PROBE 0' in m  # mainline default; diagnostic explicitly opt-in
assert m.index('#if C3_PREVIEW_PROBE\n    /* Dedicated') < m.index('MX_SDIO_SD_Init();')
assert m.index('#else\n    c3_boot_stage = 5u;') < m.index('MX_SDIO_SD_Init();')
assert '#define C3_LCD_POWER_SETTLE_MS 1000u' in m
p = subprocess.run(['nm', str(root / 'build_preview/c3m1.elf')],
                   capture_output=True, text=True, check=True).stdout
symbols = [line.split()[-1] for line in p.splitlines() if len(line.split()) >= 3]
for forbidden in ('f_mount', 'f_write', 'f_sync', 'c3_storage_save',
                  'c3_settings_flash_save', 'c3_camera_raw_init', 'c3_camera_bmp_init'):
    assert forbidden not in symbols, f'{forbidden} was linked into diagnostic firmware'
# Full LTO can inline preview_acquire into the preview loop, so its ELF
# symbol may legitimately disappear even though acquisition remains active.
assert ('c3_camera_preview_acquire' in symbols or
        'c3_camera_preview_draw' in symbols)  # live preview path linked
assert 'c3_fifo_probe_run' not in symbols
print('PASS: test-only image excludes SD, still-capture and flash-write symbols')

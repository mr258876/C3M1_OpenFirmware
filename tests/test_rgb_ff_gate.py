#!/usr/bin/env python3
"""Host regression for the narrow full-mainline SCCB 0xFF-only exception.
This verifies policy, not an electrical SCCB write or real pixel capture.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source_path = root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c'
source = source_path.read_bytes().decode('latin-1')


def take(name):
    match = re.search(r'(?:static\s+)?(?:void|uint8_t)\s+' + name + r'\s*\(', source)
    assert match, name
    brace = source.index('{', match.start())
    n = 0
    for j in range(brace, len(source)):
        if source[j] == '{': n += 1
        if source[j] == '}':
            n -= 1
            if not n: return source[match.start():j+1]
    raise RuntimeError(name)

c = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define C3_OV_WARN_MTX1 1u
#define C3_OV_WARN_MTX2 2u
volatile uint32_t c3_ov_last_verify_actual;
volatile uint32_t c3_ov_verify_warn_mask;
volatile uint32_t c3_ov_warn_first_reg, c3_ov_warn_first_expected, c3_ov_warn_first_actual;
static uint8_t actual;
static uint8_t verify_static_reg(uint8_t reg, uint8_t expected, uint8_t mask) {
    (void)reg;
    c3_ov_last_verify_actual = actual;
    return ((uint8_t)(actual ^ expected) & mask) == 0u;
}
'''
c += take('rgb_verify_or_warn_if_ff')
c += r'''
int main(void) {
    c3_ov_warn_first_reg = 0xFF;
    actual = 0xB0;
    assert(rgb_verify_or_warn_if_ff(0x94,0xB0,0xFF,C3_OV_WARN_MTX1));
    assert(!c3_ov_verify_warn_mask);
    actual = 0xFF;
    assert(rgb_verify_or_warn_if_ff(0x94,0xB0,0xFF,C3_OV_WARN_MTX1));
    assert(c3_ov_verify_warn_mask == C3_OV_WARN_MTX1);
    assert(c3_ov_warn_first_reg == 0x94 && c3_ov_warn_first_expected == 0xB0);
    assert(c3_ov_warn_first_actual == 0xFF);
    actual = 0xFF;
    assert(rgb_verify_or_warn_if_ff(0x95,0x9D,0xFF,C3_OV_WARN_MTX2));
    assert(c3_ov_verify_warn_mask == (C3_OV_WARN_MTX1|C3_OV_WARN_MTX2));
    assert(c3_ov_warn_first_reg == 0x94);
    actual = 0xAA;
    assert(!rgb_verify_or_warn_if_ff(0x95,0x9D,0xFF,C3_OV_WARN_MTX2));
    assert(c3_ov_verify_warn_mask == (C3_OV_WARN_MTX1|C3_OV_WARN_MTX2));
    puts("PASS: RGB exact match / FF warning / other mismatch fatal / first error saved");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    prog = Path(tmp)/'test.c'
    exe = Path(tmp)/'test'
    prog.write_text(c)
    subprocess.run(['cc', '-std=c11','-O2','-Wall','-Wextra','-Werror',str(prog),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

assert '#elif C3_RGB_VERIFY_WARN_ONLY' in source
assert 'if (!rgb_verify_or_warn_if_ff(MTX1' in source
assert 'if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;' in source  # strict RGB fallback & RAW
assert 'if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;' in source  # RAW
main = (root/'C3M1_Camera/Core/Src/main.c').read_text()
assert 'if (!wait_for_vsync(1600u))' in (root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
assert 'SCCB VERIFY WARN' in main
print('PASS: full-RGB-only conditional; RAW strict; VSYNC gating and warning overlay')

assert 'C3_SCCB_MAIN_HALF_US 50u' in main
assert 'c3_sccb_half_period_us = C3_SCCB_MAIN_HALF_US;' in main
assert 'C3_SCCB_MAIN_HALF_US:-50' in (root/'build_clang.sh').read_text()
print('PASS: selectable SCCB delay 20/50/100/200 us, default 50')

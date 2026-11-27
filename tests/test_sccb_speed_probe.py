#!/usr/bin/env python3
"""Compile the actual main.c SCCB speed sweep functions against a simulated bus."""
import re
import subprocess
import tempfile
from pathlib import Path
src = (Path(__file__).resolve().parents[1] / 'C3M1_Camera/Core/Src/main.c').read_text()

def extract(name):
    m = re.search(r'static void\s+' + name + r'\s*\(', src)
    assert m, name
    p = src.index('{', m.end())
    level = 0
    for end in range(p, len(src)):
        if src[end] == '{': level += 1
        if src[end] == '}':
            level -= 1
            if level == 0:
                return src[m.start():end + 1]
    raise AssertionError(name)

code = r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define OV7725_PID 0x7721u
volatile uint32_t c3_sccb_half_period_us = 5u;
volatile uint32_t c3_sccb_pid_good_5us = 0u;
volatile uint32_t c3_sccb_pid_good_20us = 0u;
volatile uint32_t c3_sccb_pid_good_50us = 0u;
volatile uint32_t c3_sccb_pid_last_5us = 0xFFFFu;
volatile uint32_t c3_sccb_pid_last_20us = 0xFFFFu;
volatile uint32_t c3_sccb_pid_last_50us = 0xFFFFu;
static unsigned transactions=0, restarts=0;
static void sccb_init(void) { restarts++; }
static void HAL_Delay(uint32_t ms) { (void)ms; }
static uint8_t ov7725_read_reg(uint16_t reg) {
    if(reg == 0x0Au) return 0x77u;
    assert(reg == 0x0Bu);
    unsigned n = transactions++;
    if (c3_sccb_half_period_us == 5u) return 0x3Fu;
    if (c3_sccb_half_period_us == 20u) return (n & 1u) ? 0x21u : 0x3Fu;
    return 0x21u;
}
'''
code += extract('c3_sccb_probe_one') + '\n' + extract('c3_sccb_probe_three_speeds')
code += r'''
int main(void) {
    c3_sccb_probe_three_speeds();
    assert(c3_sccb_pid_good_5us == 0u);
    assert(c3_sccb_pid_good_20us == 8u);
    assert(c3_sccb_pid_good_50us == 16u);
    assert(c3_sccb_pid_last_50us == 0x7721u);
    assert(c3_sccb_half_period_us == 50u);
    assert(restarts == 3u && transactions == 48u);
    puts("SCCB speed sweep test: 5=0/16, 20=8/16, 50=16/16 PASS");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    ccfile = Path(td)/'sweep.c'
    ccfile.write_text(code)
    binary = Path(td)/'sweep'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(ccfile),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)

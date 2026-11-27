#!/usr/bin/env python3
"""Host-execute the ACTUAL OV7725 SCCB access functions with a fake bus.

The fake bus deliberately returns high SDA on subaddress and payload ninth bits
(PH=6), matching a real C3M1 diagnostic observation. This exercises the
production C function bodies, not a rewritten Python version of the policy.
"""
from pathlib import Path
import subprocess, tempfile, textwrap

root = Path(__file__).resolve().parents[1]
source = (root / 'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text(encoding='utf-8')

def take(fn):
    pos = source.index(f'{fn}(')
    pos = source.index('{', pos)
    # capture from preceding function declaration
    start = source.rfind('\n', 0, source.rfind('\n', 0, pos)) + 1
    # find by exact declarations, avoiding comments/calls
    import re
    mt = list(re.finditer(r'(?:uint8_t|static uint8_t)\s+' + fn + r'\s*\(', source[:pos]))[-1]
    start = mt.start()
    depth = 0
    for end in range(pos, len(source)):
        if source[end] == '{': depth += 1
        elif source[end] == '}':
            depth -= 1
            if depth == 0: return source[start:end+1]
    raise AssertionError(fn)

c = '''
#include <stdint.h>
/* v178 verify_static_reg also records the actual COM13 byte. */
#define COM13 0x3Eu
static uint8_t blc_last_readback;
#include <stdio.h>
#include <assert.h>
#define OV7725_ADDR 0x42u
volatile uint32_t c3_ov_last_bad_reg = 0xFFu;
volatile uint32_t c3_ov_last_bad_phase = 0u;
volatile uint32_t c3_ov_last_verify_expected = 0xFFu;
volatile uint32_t c3_ov_last_verify_actual = 0xFFu;
volatile uint32_t c3_ov_last_dc_reg = 0xFFu;
volatile uint32_t c3_ov_last_dc_bits = 0u;
static uint8_t registers[256], phase_bytes[3], selected_reg;
static uint8_t phase, connected=1u;
static unsigned starts, stops, reads, writes;
static void sccb_start(void) { ++starts; phase=0; }
static uint8_t sccb_send_byte(uint8_t byte) {
    assert(phase < 3);
    phase_bytes[phase] = byte;
    unsigned at = phase++;
    if (at == 1u) selected_reg = byte;
    /* PH6 as seen on real hardware: ninth bits for reg and data float high */
    return (uint8_t)(at == 1u || at == 2u);
}
static void sccb_stop(void) {
    ++stops;
    if (phase == 3 && phase_bytes[0] == OV7725_ADDR) {
        if (connected) registers[phase_bytes[1]] = phase_bytes[2];
        ++writes;
    }
}
static uint8_t sccb_read_byte(void) { ++reads; return registers[selected_reg]; }
static void sccb_nack(void) {}
static void delay_ms(uint32_t ms) { (void)ms; }
'''
c += '\n'.join(take(n) for n in ('ov7725_read_reg', 'ov7725_read_reg_checked', 'ov7725_write_reg', 'verify_static_reg'))
c += '''
int main(void) {
    uint8_t got=0;
    assert(ov7725_write_reg(0x94u, 0xB0u) == 0u);
    assert(c3_ov_last_dc_reg == 0x94u && c3_ov_last_dc_bits == 6u);
    assert(writes==1u && registers[0x94]==0xB0u);
    assert(ov7725_read_reg_checked(0x94u,&got)==1u && got == 0xB0u);
    assert(verify_static_reg(0x94u,0xB0u,0xFFu)==1u);
    /* True failure is detected by readback mismatch, not PH6. */
    connected=0u;
    assert(ov7725_write_reg(0x94u, 0xAAu)==0u);
    assert(verify_static_reg(0x94u,0xAAu,0xFFu)==0u);
    assert(c3_ov_last_bad_phase==8u && c3_ov_last_bad_reg==0x94u);
    assert(c3_ov_last_verify_expected==0xAAu && c3_ov_last_verify_actual==0xB0u);
    assert(ov7725_read_reg_checked(0x10u,0)==0u);
    puts("SCCB policy test: PH6 allowed, register readback verified, mismatch detected");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    cpath = Path(td)/'test.c'
    exe = Path(td)/'test'
    cpath.write_text(c)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(cpath),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

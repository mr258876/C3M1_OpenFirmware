#!/usr/bin/env python3
"""Execute actual v145 C SCCB alternate transaction + static verification in a bus mock."""
from pathlib import Path
import re, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
ss=(root/'C3M1_Camera/Drivers/BSP/OV7725/sccb.c').read_bytes().decode('latin1')
ov=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()

def take(src,fn):
    matches=list(re.finditer(r'(?:static\s+)?uint8_t\s+'+re.escape(fn)+r'\s*\(',src))
    assert matches,fn
    start=matches[-1].start()
    brace=src.index('{',start)
    n=0
    for i in range(brace,len(src)):
        if src[i]=='{':n+=1
        elif src[i]=='}':
            n-=1
            if n==0:return src[start:i+1]
    raise AssertionError(fn)

text=r'''
#include <stdint.h>
/* v178 verify_static_reg also records the actual COM13 byte. */
#define COM13 0x3Eu
static uint8_t blc_last_readback;
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define C3_SCCB_RESTART_VERIFY 1
#define OV7725_ADDR 0x42u
static unsigned starts,stops,bytes,reads,nacks,releases;
static uint8_t sent[3], read_value=0xFFu, old_value=0xFFu;
static uint8_t sccb_send_byte(uint8_t b){ sent[bytes%3u]=b; ++bytes; return 1u; }
static void sccb_start(void){++starts;}
static void sccb_stop(void){++stops;}
static void sccb_nack(void){++nacks;}
#define SCCB_SDA(v) do { if(v) ++releases; } while(0)
static uint8_t sccb_read_byte(void){++reads;return read_value;}
static uint8_t ov7725_read_reg(uint16_t r){(void)r; return old_value;}
static void delay_ms(uint32_t ms){(void)ms;}
volatile uint32_t c3_sccb_restart_attempts=0, c3_sccb_restart_successes=0,
                  c3_sccb_restart_last_reg=0xff;
volatile uint32_t c3_ov_last_bad_reg=0xff,c3_ov_last_bad_phase,
                  c3_ov_last_verify_expected=0xff,c3_ov_last_verify_actual=0xff;
'''
text+='\n'+take(ss,'sccb_read_reg_restart')+'\n'+take(ov,'ov7725_read_reg_restart')+'\n'+take(ov,'verify_static_reg')+'\n'
text+=r'''
int main(void){
    /* Old STOP/START read was FF, but repeated START returns the expected B0. */
    assert(verify_static_reg(0x94u,0xB0u,0xFFu)==0u);
    /* The mock is FF for both; strict verification must fail. */
    assert(c3_ov_last_bad_reg==0x94u && c3_ov_last_verify_actual==0xFFu);
    assert(c3_sccb_restart_attempts==2u && c3_sccb_restart_successes==0u);
    assert(starts==4u && stops==2u && reads==2u && nacks==2u && releases==2u);
    assert(sent[0]==0x42u && sent[1]==0x94u && sent[2]==0x43u);
    read_value=0xB0u;
    assert(verify_static_reg(0x94u,0xB0u,0xFFu)==1u);
    assert(c3_sccb_restart_successes==1u);
    read_value=0x40u;
    assert(verify_static_reg(0x67u,0x02u,0x03u)==0u);
    assert(c3_ov_last_bad_reg==0x67u && c3_ov_last_verify_actual==0x40u);
    /* Normal legacy matches should not invoke alternate SCCB mode. */
    unsigned before=c3_sccb_restart_attempts;
    old_value=0x02u;
    assert(verify_static_reg(0x67u,0x02u,0x03u)==1u);
    assert(c3_sccb_restart_attempts==before);
    puts("v145 SCCB host tests: alternate transaction, strict compare, bounded retry OK");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    p=Path(td)/'test.c';o=Path(td)/'test'
    p.write_text(text)
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(o)],check=True)
    subprocess.run([str(o)],check=True)

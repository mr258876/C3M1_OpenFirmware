#!/usr/bin/env python3
"""Compile actual wait, ISR, stop and final capture functions under a mock
clock/EXTI/GPIO and the real scheduler. Not an electrical/ABLC model."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
core=root/'C3M1_Camera/Core'
cam=(core/'Src/c3_camera.c').read_text()
main=(core/'Src/main.c').read_text()
def fn(signature):
    i=cam.index(signature); a=cam.index('{',i); depth=1; j=a+1
    while depth:
        depth += (cam[j]=='{')-(cam[j]=='}'); j+=1
    return cam[i:j]
head=r'''
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include "c3_frame_sched.h"
#define C3_PREVIEW_PROFILE 0
#define GPIO_PIN_3 8u
#define OV7725_VSYNC_Pin 256u
struct gpio { uint32_t BRR, BSRR; } gpio;
#define GPIOB (&gpio)
static c3_frame_sched_t frame_sched;
static uint8_t raw_frame_held;
static uint32_t c3_camera_vsync_last_ms, c3_camera_vsync_edges;
static uint32_t tick, elapsed, period, next_edge, irq_masked, pending, clears;
static uint32_t remaining_edges, resets, poll_clock, in_isr, writes_started;
static uint8_t check_idle;
void c3_camera_vsync_isr(void);
static void deliver(void) {
    if (irq_masked || !pending) return;
    pending=0; in_isr=1; c3_camera_vsync_isr(); in_isr=0;
    if (frame_sched.writing) ++writes_started;
    if (check_idle) {
        assert(!frame_sched.writing && !frame_sched.armed && !frame_sched.ready);
        assert(!gpio.BSRR && gpio.BRR==GPIO_PIN_3);
    }
}
static void advance(void) {
    ++tick; ++elapsed;
    if (elapsed==next_edge && remaining_edges) {
        --remaining_edges; next_edge+=period; pending=1; deliver();
    }
}
static uint32_t HAL_GetTick(void) {
    if(poll_clock && !in_isr) advance();
    return tick;
}
static void HAL_Delay(uint32_t ms) { while (ms--) advance(); }
static uint32_t irq_lock(void) {uint32_t old=irq_masked; irq_masked=1; return old;}
static void irq_unlock(uint32_t old) {irq_masked=old; deliver();}
static void clear_exti(uint32_t pin) {assert(irq_masked && pin==OV7725_VSYNC_Pin); pending=0; ++clears;}
#define __HAL_GPIO_EXTI_CLEAR_IT(pin) clear_exti(pin)
static void write_pointer_reset(void) {++resets; gpio.BRR=GPIO_PIN_3; HAL_Delay(1);}
'''
body=r'''
static void setup(uint32_t p,uint32_t phase,uint32_t limit) {
    tick=elapsed=irq_masked=pending=clears=resets=poll_clock=in_isr=writes_started=0;
    period=p; next_edge=phase; remaining_edges=limit;
    raw_frame_held=0; gpio.BRR=gpio.BSRR=0;
    check_idle=1; c3_camera_vsync_edges=c3_camera_vsync_last_ms=0;
    c3_sched_reset(&frame_sched,C3_SCHED_STILL);
}
int main(void) {
    const unsigned nvals[]={1,2,3,6};
    const unsigned periods[]={34,43,64,333};
    unsigned cases=0;
    for(unsigned p=0;p<sizeof(periods)/sizeof(periods[0]);p++) {
        unsigned T=periods[p];
        for(unsigned i=0;i<4;i++) for(unsigned phase=1;phase<=T;phase++) {
            unsigned n=nvals[i]; setup(T,phase,100);
            /* Old pending VSYNC and a nonzero/wrapping edge count must not
             * count as an interval after the final exposure writes. */
            pending=1; frame_sched.edges=UINT32_MAX-2u; tick=UINT32_MAX-10u;
            assert(c3_camera_wait_still_frames(n,1200u));
            assert(elapsed==phase+n*T && clears==1 && resets==0);
            assert((uint32_t)(frame_sched.edges-(UINT32_MAX-2u))==n+1u);
            assert(!frame_sched.armed && !frame_sched.ready && !frame_sched.writing);
            assert(!writes_started);
            /* Post-settle SCCB verification may consume additional VSYNCs.
             * They must not arm the FIFO; only the final capture can. */
            HAL_Delay(T+3u); assert(!writes_started);
            check_idle=0; poll_clock=1;
            assert(c3_camera_capture(1200u));
            assert(frame_sched.ready==1 && !frame_sched.writing);
            assert(writes_started==1 && resets==1 && clears==2);
            ++cases;
        }
    }
    /* Complete lack of VSYNC and loss after some valid frames. */
    for(unsigned edges=0;edges<=6;edges++) {
        setup(43,20,edges);
        assert(!c3_camera_wait_still_frames(6,1200));
        assert(elapsed==(edges?20+(edges-1)*43:0)+1200);
        assert(frame_sched.mode==C3_SCHED_OFF && !writes_started && !resets);
        assert(!c3_camera_capture(1200));
    }
    setup(43,20,100); assert(c3_camera_wait_still_frames(0,1200)); assert(elapsed==0);
    setup(43,20,100); assert(!c3_camera_wait_still_frames(7,1200));
    assert(!c3_camera_wait_still_frames(2,0)); assert(!clears && !resets);
    setup(43,20,100); raw_frame_held=1;
    assert(!c3_camera_wait_still_frames(1,1200) && !clears && raw_frame_held);
    setup(43,20,100); c3_sched_arm_still(&frame_sched);
    assert(!c3_camera_wait_still_frames(1,1200) && !clears && frame_sched.armed);
    setup(43,20,100); c3_sched_reset(&frame_sched,C3_SCHED_PREVIEW);
    assert(!c3_camera_wait_still_frames(1,1200) && !clears);
    setup(43,20,100); frame_sched.ready=1;
    assert(!c3_camera_wait_still_frames(1,1200) && !clears && frame_sched.ready);
    printf("PASS v180: %u phase/period/variant cases, stale IRQ, tick/edge wrap, WEN off, one final capture, lost VSYNC and held/busy frame guards\n",cases);
    return 0;
}
'''
code=head+'\n'.join(fn(x) for x in ['static void scheduler_stop(void)','void c3_camera_vsync_isr(void)','uint8_t c3_camera_wait_still_frames(','uint8_t c3_camera_capture('])+body
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'test.c'; p.write_text(code); exe=p.with_suffix('')
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(core/'Inc'),str(p),str(core/'Src/c3_frame_sched.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
# Integration: two DNG paths; keep legacy BMP and legacy opt-out, wait fail
# skips the gate/final capture/save, gate failure skips final capture/save.
assert main.count('ok = c3_camera_wait_still_frames(settle, 1200u);')==2
assert main.count('settle = C3_DNG_FAST_SETTLE_FRAMES;')==1
assert 'if (c3_camera_raw_fast_path_used && raw_locked &&\n                        !exposure_changed && c3_ov_verify_warn_mask == 0u)' in main
for part in main.split('ok = c3_camera_wait_still_frames(settle, 1200u);')[1:]:
    block=part[:part.index('c3_storage_save(pending_format)')]
    assert 'for (unsigned i = 0; i < settle; ++i)' in block
    assert 'if (ok && pending_format == C3_OUTPUT_DNG &&\n                    !ov7725_verify_raw_blc_capture())' in block
    assert '} else if (ok && c3_camera_capture(1200u) && c3_camera_raw_hold())' in block
    assert 'c3_dng_capture_black_level(' in block
assert 'unsigned settle = fast_bmp ? 0u' in main
print('PASS v180 integration: DNG-only wait, 6-frame fallbacks, strict BLC gate, final capture/hold before LUT snapshot/save, BMP unchanged')

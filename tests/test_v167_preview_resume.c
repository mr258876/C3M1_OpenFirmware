#include "c3_exposure.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static uint8_t regs[256], writes[256];
static unsigned n;
static uint8_t wr(uint8_t reg,uint8_t value)
{
    regs[reg]=value;
    writes[n++]=reg;
    return 0u;
}
static void clear_mock(void) { n=0; for(unsigned i=0;i<256;++i) regs[i]=writes[i]=0; }
static void assert_writes(unsigned count,uint8_t first,uint8_t second,uint8_t third)
{
    assert(n==count);
    if(n>0) assert(writes[0]==first);
    if(n>1) assert(writes[1]==second);
    if(n>2) assert(writes[2]==third);
}
int main(void)
{
    c3_exposure_t p;
    c3_exposure_init(&p,9u,0u);
    c3_exposure_snapshot_t s={.valid=1u,.preview_rows=485u,.gain_register=0x23u};
    /* Program AEC/GAIN while the caller still holds the auto-control bits off. */
    clear_mock();
    assert(c3_exposure_effective_dial(&p)==C3_EXPOSURE_AUTO_CODE);
    assert(c3_exposure_restore_preview_snapshot(&s,&p,wr));
    assert_writes(3,0x08,0x10,0x00);
    assert(regs[0x08]==1u && regs[0x10]==229u && regs[0x00]==0x23u);

    /* A valid snapshot is not guaranteed; no guessed values are emitted. */
    s.valid=0u; clear_mock();
    assert(c3_exposure_restore_preview_snapshot(&s,&p,wr));
    assert_writes(0,0,0,0);

    /* Manual shutter/ISO selection must NOT be overwritten with previous
       automatic exposure (c3_exposure_apply will install the selected value). */
    s.valid=1u;
    c3_exposure_sync_dial(&p,7u,10u);
    c3_exposure_gain_next(&p); c3_exposure_gain_next(&p);
    clear_mock();
    assert(c3_exposure_restore_preview_snapshot(&s,&p,wr));
    assert_writes(0,0,0,0);

    /* S: manual shutter / auto gain restores only AGC. */
    for(unsigned i=0;i<4;i++) c3_exposure_gain_next(&p);
    clear_mock();
    assert(c3_exposure_restore_preview_snapshot(&s,&p,wr));
    assert_writes(1,0x00,0,0);
    assert(!c3_exposure_restore_preview_snapshot(&s,0,wr));
    assert(!c3_exposure_restore_preview_snapshot(&s,&p,0));
    puts("PASS v167: preview AEC/AGC snapshot restores only auto-controlled values");
}

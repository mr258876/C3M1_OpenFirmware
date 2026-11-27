#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c3_fifo_dma_load_plan.h"
#include "c3_peak_experiment.h"
/* Host-only logical pipeline check; no claim of electrical timing. */
static uint16_t raw[2][C3_FD_BYTES];
static uint8_t out[2][C3_FD_BYTES], display[C3_FD_BYTES];
static unsigned raw_row[2], out_row[2];
static c3_focus_t focus;
static uint8_t pixel(unsigned y,unsigned x){return (uint8_t)((y*71u+x*19u+(y^x))&255u);}
static void prep(unsigned y,unsigned peak){
    unsigned i=c3_fl_slot(y); assert(raw_row[i]==y);
    for(unsigned x=0;x<C3_FD_BYTES;++x)out[i][x]=c3_fd_unpack(raw[i][x]);
    if(peak)c3_peak_fast_pixels(out[i],(uint16_t)y,C3_PEAK_AREA_FULL,&focus,16,0xf800,0,0);
    out_row[i]=y;
}
int main(void){
    const unsigned freq[]={400,450,480,514,554,600,655};
    unsigned combos=0,rows=0,overlap=0,checks=0;
    for(unsigned m=0;m<C3_FL_NMODE;++m)for(unsigned f=0;f<C3_FL_NFREQ;++f)for(unsigned p=0;p<C3_FL_NPHASE;++p){
        c3_fd_plan_t t=c3_fl_plan(f,p); ++combos;
        assert(c3_fm_freq100(t)==freq[f] && t.sample_tick==p+1 && t.high_ticks+2==t.ticks);
        for(unsigned pulse=639;pulse<=641;++pulse)for(unsigned ndtr=0;ndtr<=1;++ndtr)for(unsigned te=0;te<=1;++te)for(unsigned tc=0;tc<=1;++tc)for(unsigned pin=0;pin<=1;++pin){
            unsigned want=te?1:pulse<640?2:pulse>640?3:ndtr||!tc?5:!pin?6:0;
            assert(c3_fh_check(te,0,pulse,0,0,t.high_ticks,ndtr,tc,pin)==want);++checks;
        }
        memset(&focus,0,sizeof(focus)); raw_row[0]=raw_row[1]=out_row[0]=out_row[1]=240;
        unsigned sent=0;
        for(unsigned y=0;y<C3_FD_ROWS;++y){
            unsigned i=c3_fl_slot(y);
            if((m&C3_FL_LCD)&&y>=2){assert(out_row[i]==y-2);memcpy(display,out[i],sizeof(display));assert(sent++==y-2);++overlap;}
            for(unsigned x=0;x<C3_FD_BYTES;++x)raw[i][x]=(uint16_t)((pixel(y,x)<<1u)|0x8001u);
            raw_row[i]=y;
            if((m&C3_FL_CPU)&&y>=1){assert(c3_fl_slot(y-1)!=i);prep(y-1,1);}
            if((m&C3_FL_LCD)&&y>=2)assert(memcmp(display,out[i],sizeof(display))==0);
            for(unsigned x=0;x<C3_FD_BYTES;++x)assert(c3_fd_unpack(raw[i][x])==pixel(y,x));
            if((m&C3_FL_LCD)&&!(m&C3_FL_CPU))prep(y,0);
            ++rows;
        }
        if(m&C3_FL_LCD){if(m&C3_FL_CPU)prep(239,1);assert(out_row[0]==238&&out_row[1]==239&&sent==238);}
    }
    assert(combos==56&&rows==13440&&overlap==6664&&C3_FL_REPEATS==32);
    assert(!c3_fl_plan(7,0).ticks&&!c3_fl_plan(0,2).ticks);
    printf("PASS: %u load combinations, %u logical pipeline rows, %u LCD overlap slots, %u hardware status snapshots\n",combos,rows,overlap,checks);
}

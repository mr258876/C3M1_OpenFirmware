#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_preview_dma_render.h"
static uint32_t r=0x129be88;
static uint32_t rnd(void){r^=r<<13;r^=r>>17;r^=r<<5;return r;}
int main(void){
 for(unsigned cfg=0;cfg<32;cfg++){
  c3_focus_t f;c3_score_t s;c3_peak_exp_t exp={0};c3_focus_reset(&f);c3_score_reset(&s);
  for(unsigned frame=0;frame<5;frame++){
   unsigned peak=(cfg>>3)&1u,mode=(cfg>>4)&1u?C3_SCORE_FULL_SPARSE:0u,area=C3_PEAK_AREA_FULL_SPARSE,shared=peak&&c3_peak_exp_shared(area,mode);
   unsigned sample=c3_score_schedule_frame(&s,mode,shared);c3_peak_exp_begin(&exp,area,peak);if(peak)c3_focus_begin_frame(&f);
   c3_live_render_t p={.grid=cfg&1u,.mono=(cfg>>1)&1u,.peaking=peak,.score_sample=sample,.score_shared=shared,.score_mode=mode,.peak_area=area,.threshold=f.threshold,.color=0xf800};
   for(unsigned y=0;y<240;y++){
    uint8_t row[640],mask[40];for(unsigned x=0;x<640;x++)row[x]=(uint8_t)rnd();for(unsigned x=0;x<40;x++)mask[x]=x%3?(uint8_t)rnd():0;
    c3_live_render_row(row,y,(cfg&4u)&&(y<24||y>=216)?mask:NULL,&p,&f,&s,&exp);
    if(fwrite(row,1,sizeof row,stdout)!=sizeof row)return 1;
   }
   if(peak)c3_focus_end_frame_area(&f,c3_peak_exp_roi_count(area));if(sample)c3_score_end_frame(&s);
   uint32_t v[]={f.roi_sum,f.threshold,f.score,s.sum,s.count,s.score};fwrite(v,1,sizeof v,stdout);
  }
 }
 return ferror(stdout)?1:0;
}

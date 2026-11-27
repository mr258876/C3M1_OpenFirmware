#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_fifo_dma_live_plan.h"
#include "c3_preview_dma_render.h"
static uint8_t raw[640]; static unsigned cursor;
static uint8_t fifo(void){assert(cursor<640);return raw[cursor++];}
#define C3_ROW_FIFO_BYTE() fifo()
#define C3_ROW_DIM565(c) c3_effect_dim565(c)
#include "c3_preview_row_opt.h"
static uint32_t seed=0x1f962198u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
/* Independent control path from v197 c3_camera_preview_draw, including
 * fused OSD and grid-only CPU paths. This is a comparison of renderers,
 * not a physical DMA emulator. */
static void ref_render(uint8_t *out,uint16_t y,const uint8_t *mask,const c3_live_render_t *p,c3_focus_t *f,c3_score_t *s,c3_peak_exp_t *exp){
 cursor=0;
 if(p->peaking){
  uint8_t peak_row=c3_peak_exp_row_active(p->peak_area,y,exp->phase);
  uint8_t score_row=p->score_sample&&!p->score_shared&&c3_score_row_active(p->score_mode,y);
  if(!peak_row&&!score_row&&!p->mono){
   if(mask){c3_row_copy_osd(out,mask);if(p->grid)c3_peak_fast_dimmed_grid(out,mask);}
   else{c3_row_copy_plain(out);if(p->grid)c3_effect_grid_row(out,y,0);}
   if(p->score_center)c3_effect_score_corners(out,y);
  }else{
   c3_row_copy_plain(out);
   if(score_row)c3_score_sample_row(s,y,out);
   c3_peak_exp_pixels(out,y,p->peak_area,f,p->threshold,p->color,p->mono,p->score_shared&&c3_peak_sparse(p->peak_area)?s:0,exp);
   c3_peak_fast_overlay(out,y,p->grid,p->score_center,mask);
  }
 }else if(p->score_sample&&c3_score_row_active(p->score_mode,y)){
  c3_row_copy_plain(out);c3_score_sample_row(s,y,out);c3_effect_score_line(out,y,p->mono,p->grid,0,mask);
 }else if(p->grid&&!p->mono){
  if(mask)c3_row_copy_osd(out,mask);else c3_row_copy_plain(out);
  c3_effect_grid_row(out,y,mask);
 }else if(!mask&&!p->grid&&!p->mono)c3_row_copy_plain(out);
 else if(mask&&!p->grid&&!p->mono)c3_row_copy_osd(out,mask);
 else for(unsigned x=0;x<320;x++){
  uint16_t hi=fifo(),v=(uint16_t)(hi<<8|fifo());
  uint16_t c=p->mono?c3_effect_gray565(v):v;
  if(mask){c=c3_effect_dim565(c);if(p->grid&&(x==106||x==213))c=c3_effect_grid565(c);if(c3_effect_glyph(mask,x))c=0xffff;}
  else if(p->grid&&(y==80||y==160||x==106||x==213))c=c3_effect_grid565(c);
  c3_effect_put565(out,x,c);
 }
 if(!p->peaking&&p->score_center)c3_effect_score_corners(out,y);
 assert(cursor==640);
}
static void test_blocks(unsigned rows){
 const unsigned n=rows*640, blocks=240/rows;
 /* A dry run using explicit ownership of both buffers. */
 unsigned raw_ready[2]={0,0},out_ready[2]={0,0},sent=0,processed=0,read=0;
 raw_ready[0]=1;read+=n;
 for(unsigned b=0;b<blocks;b++){
  unsigned slot=c3_live_slot(b),next=b+1<blocks,prev=b!=0;
  assert(raw_ready[slot]==b+1);
  if(next){assert(c3_live_slot(b+1)!=slot);assert(raw_ready[slot^1]==0);}
  if(prev){assert(c3_live_slot(b-1)!=slot);assert(out_ready[slot^1]==b);}
  assert(!out_ready[slot]);raw_ready[slot]=0;out_ready[slot]=b+1;processed+=n;
  if(next){raw_ready[slot^1]=b+2;read+=n;}
  if(prev){out_ready[slot^1]=0;sent+=n;}
 }
 assert(out_ready[c3_live_slot(blocks-1)]==blocks);sent+=n;
 assert(sent==153600&&processed==sent&&read==sent);
 assert(c3_live_check(n,0,n,2,2,10,0,1,1)==0);
 assert(c3_live_check(n,1,n,2,2,10,0,1,1)==1);
 assert(c3_live_check(n,0,n-1,2,2,10,0,1,1)==2);
 assert(c3_live_check(n,0,n+1,2,2,10,0,1,1)==3);
 assert(c3_live_check(n,0,n,2,3,10,0,1,1)==4);
 assert(c3_live_check(n,0,n,10,10,10,0,1,1)==4);
 assert(c3_live_check(n,0,n,2,2,10,1,1,1)==5);
 assert(c3_live_check(n,0,n,2,2,10,0,0,1)==5);
 assert(c3_live_check(n,0,n,2,2,10,0,1,0)==6);
}
int main(void){
 test_blocks(1);test_blocks(4);
 for(unsigned a=0;a<65536;a++){
  unsigned b=65535-a; uint32_t v=a|(b<<16);
  uint16_t got=c3_live_unpack_pair(v),want=(uint16_t)(c3_fd_unpack(a)|((uint16_t)c3_fd_unpack(b)<<8));assert(got==want);
 }
 unsigned frames=0,lines=0;
 for(unsigned peak=0;peak<=4;peak++)for(unsigned score=0;score<5;score++)for(unsigned cfg=0;cfg<8;cfg++){
  c3_focus_t f1,f2;c3_score_t s1,s2;c3_peak_exp_t e1={0},e2={0};
  c3_focus_reset(&f1);c3_focus_reset(&f2);c3_score_reset(&s1);c3_score_reset(&s2);
  for(unsigned k=0;k<3;k++){
   c3_live_render_t p={.grid=(cfg>>1)&1,.mono=cfg&1,.peaking=!!peak,.score_center=c3_score_is_center(score),.score_mode=score,.peak_area=peak?peak-1:0,.threshold=f1.threshold,.color=0xf800};
   p.score_shared=p.peaking&&c3_peak_exp_shared(p.peak_area,p.score_mode);
   p.score_sample=c3_score_schedule_frame(&s1,score,p.score_shared);assert(p.score_sample==c3_score_schedule_frame(&s2,score,p.score_shared));
   c3_peak_exp_begin(&e1,p.peak_area,p.peaking);c3_peak_exp_begin(&e2,p.peak_area,p.peaking);
   if(peak){c3_focus_begin_frame(&f1);c3_focus_begin_frame(&f2);}
   for(unsigned y=0;y<240;y++){
    _Alignas(4) uint8_t o1[640],o2[640],bits[40];
    for(unsigned x=0;x<640;x++)raw[x]=(uint8_t)rnd();
    for(unsigned i=0;i<40;i++)bits[i]=i%3?(uint8_t)rnd():0;
    const uint8_t *mask=(cfg&4)&&(y<24||y>=216)?bits:0;
    memcpy(o1,raw,640);ref_render(o2,y,mask,&p,&f2,&s2,&e2);c3_live_render_row(o1,y,mask,&p,&f1,&s1,&e1);
    assert(!memcmp(o1,o2,640));lines++;
   }
   assert(f1.roi_sum==f2.roi_sum&&!memcmp(f1.prev_row,f2.prev_row,320));
   if(peak){c3_focus_end_frame_area(&f1,c3_peak_exp_roi_count(p.peak_area));c3_focus_end_frame_area(&f2,c3_peak_exp_roi_count(p.peak_area));}
   if(p.score_shared&&!c3_peak_sparse(p.peak_area)){c3_score_end_shared_center(&s1,f1.roi_sum);c3_score_end_shared_center(&s2,f2.roi_sum);}else if(p.score_sample){c3_score_end_frame(&s1);c3_score_end_frame(&s2);}
   assert(f1.score==f2.score&&f1.threshold==f2.threshold);assert(s1.count==s2.count&&s1.sum==s2.sum&&s1.score==s2.score);frames++;
  }
 }
 printf("PASS: %u frames / %u lines vs original renderer; 65536 packed pairs; both block sizes ownership and transfer fault checks\n",frames,lines);
}

/* Dense exact-output regression vs v190 renderers; sparse independent oracle;
 * production FIFO row helpers, real score path & shared score cadence. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_preview_peak_fast.h"
#include "c3_preview_peak_center.h"
static uint8_t raw[640];
static unsigned cursor;
static uint8_t fifo(void){assert(cursor<640u);return raw[cursor++];}
#define C3_ROW_FIFO_BYTE() fifo()
#define C3_ROW_DIM565(c) c3_effect_dim565(c)
#include "c3_preview_row_opt.h"
static uint16_t image[240][320];
static uint32_t seed=0x52b2ac97u;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static unsigned lum(uint16_t p){return (4u*((p>>11)&31u)+5u*((p>>5)&63u)+2u*(p&31u))/8u;}
static unsigned edge(unsigned x,unsigned y){int a=lum(image[y][x]);return (unsigned)(__builtin_abs(a-(int)lum(image[y][x-1]))+__builtin_abs(a-(int)lum(image[y-1][x])));}
static void pattern(unsigned kind){
 for(unsigned y=0;y<240;y++)for(unsigned x=0;x<320;x++){
    uint16_t p=(uint16_t)rnd();
    if(kind==0)p=0;
    if(kind==1)p=((x^y)&1)?0xffff:0;
    if(kind==2)p=(x/9+y/11)&1?0x7bef:0x39e7;
    if(kind==3)p=(uint16_t)(x*31+y*593);
    image[y][x]=p;
 }
}
static void oracle(uint8_t *out,unsigned y,unsigned area,unsigned mono,unsigned grid,unsigned corners,const uint8_t *mask,unsigned threshold,uint16_t color,uint32_t *sum){
 unsigned center=area==C3_PEAK_AREA_CENTER||area==C3_PEAK_AREA_CENTER_SPARSE;
 unsigned sparse=area==C3_PEAK_AREA_CENTER_SPARSE||area==C3_PEAK_AREA_FULL_SPARSE;
 for(unsigned x=0;x<320;x++){
    unsigned active=y>=(center?60u:24u)&&y<(center?180u:216u)&&x>=(center?80u:(sparse?2u:1u))&&x<(center?240u:320u)&&(!sparse||(!(x&1u)&&!(y&1u)));
    uint16_t c=image[y][x];
    if(mono)c=c3_effect_gray565(c);
    if(active){unsigned e=edge(x,y);if(x>=80&&x<240&&y>=60&&y<180)*sum+=e;if(lum(image[y][x])>3&&e>=threshold)c=color;}
    if(grid&&(y==80||y==160||x==106||x==213))c=c3_effect_grid565(c);
    if(corners&&c3_focus_roi_corner(x,y))c=0x07e0;
    if(mask)c=c3_effect_dim565(c);
    if(c3_effect_glyph(mask,x))c=0xffff;
    c3_effect_put565(out,x,c);
 }
}
static void render(uint8_t *out,unsigned y,unsigned area,c3_focus_t *f,unsigned threshold,uint16_t color,unsigned mono,unsigned grid,unsigned corners,const uint8_t *mask,c3_score_t *score,unsigned sample,unsigned shared){
 cursor=0;
 unsigned score_row=sample&&!shared&&c3_score_row_active(score->mode,y);
 if(!c3_peak_row_active(area,y)&&!score_row&&!mono){
    if(mask){c3_row_copy_osd(out,mask);if(grid)c3_peak_fast_dimmed_grid(out,mask);}
    else {c3_row_copy_plain(out);if(grid)c3_effect_grid_row(out,y,0);}
    if(corners)c3_effect_score_corners(out,y);
 }else{
    c3_row_copy_plain(out);
    if(score_row)c3_score_sample_row(score,y,out);
    c3_peak_fast_pixels(out,y,area,f,threshold,color,mono,shared&&c3_peak_sparse(area)?score:NULL);
    c3_peak_fast_overlay(out,y,grid,corners,mask);
 }
 assert(cursor==640);
}
int main(void){
 unsigned frames=0,rows=0;
 static const unsigned active_rows[]={121,193,120,192};
 static const unsigned gradient_points[]={19200,61248,4800,15264};
 static const unsigned score_points[]={0,19200,1200,76241,4800};
 for(unsigned a=0;a<4;a++){
   unsigned active=0,n=0;
   for(unsigned y=0;y<240;y++){
     unsigned center=(a==0||a==2),sp=(a>=2),y0=center?60:24,y1=center?180:216;
     unsigned ref=0;for(unsigned r=y0;r<y1;r+=sp?2:1)if(y==r||y==r-1)ref=1;
     assert(c3_peak_row_active(a,y)==ref);active+=ref;
     if(y>=y0&&y<y1&&(!sp||!(y&1)))n+=center?(sp?80:160):(sp?159:319);
   }
   assert(active==active_rows[a]&&n==gradient_points[a]);
   assert(c3_peak_roi_count(a)==(a>=2?4800u:19200u));
 }
 for(unsigned i=0;i<65536;i++){
    uint8_t p[2]={(uint8_t)(i>>8),(uint8_t)i};assert(c3_luma_at(p)==lum(i));
    /* Exact grid-on-dim order over every RGB565 value. */
    assert(c3_effect_dim565(c3_effect_grid565(i))==c3_effect_dim565((uint16_t)(c3_effect_dim565(i)+0x7befu)));
 }
 for(unsigned a=0;a<4;a++)for(unsigned m=0;m<5;m++)for(unsigned cfg=0;cfg<8;cfg++){
   unsigned mono=cfg&1,grid=(cfg>>1)&1,osd=(cfg>>2)&1;
   c3_focus_t f,old;c3_focus_reset(&f);c3_focus_reset(&old);
   c3_score_t score,ref;c3_score_reset(&score);c3_score_reset(&ref);
   unsigned shared=c3_peak_score_shared(a,m),corners=c3_score_is_center(m);
   for(unsigned k=0;k<5;k++){
      pattern(k);uint16_t color=(uint16_t)(0xf800u^(cfg*0x421u));
      unsigned sample=c3_score_schedule_frame(&score,m,shared);
      c3_score_begin_frame(&ref,m);
      uint8_t threshold=f.threshold;
      assert(a>=2||f.threshold==old.threshold);
      c3_focus_begin_frame(&f);c3_focus_begin_frame(&old);uint32_t sum=0;
      for(unsigned y=0;y<240;y++){
        _Alignas(4) uint8_t outbuf[672],b[640],legacy[640],bits[40];
        uint8_t *out=outbuf+16;memset(outbuf,0xa5,sizeof(outbuf));
        for(unsigned x=0;x<320;x++)c3_effect_put565(raw,x,image[y][x]);
        for(unsigned j=0;j<40;j++)bits[j]=j%3==0?0:(uint8_t)rnd();
        const uint8_t *mask=osd&&(y<24||y>=216)?bits:NULL;
        if(sample)c3_score_sample_row(&ref,y,raw);
        oracle(b,y,a,mono,grid,corners,mask,threshold,color,&sum);
        render(out,y,a,&f,threshold,color,mono,grid,corners,mask,&score,sample,shared);
        assert(!memcmp(out,b,640));
        if(a<2){memcpy(legacy,raw,640);if(a==0)c3_effect_peak_line_center(legacy,y,&old,threshold,color,mono,grid,corners,mask);else c3_effect_peak_line_staged(legacy,y,&old,threshold,color,mono,grid,corners,mask);assert(!memcmp(out,legacy,640));assert(f.roi_sum==old.roi_sum);}
        for(unsigned j=0;j<16;j++)assert(outbuf[j]==0xa5&&outbuf[656+j]==0xa5);
        ++rows;
      }
      assert(f.roi_sum==sum);
      c3_focus_end_frame_area(&f,c3_peak_roi_count(a));
      if(a<2){c3_focus_end_frame(&old);assert(f.score==old.score&&f.threshold==old.threshold);}
      if(shared&&a<2)c3_score_end_shared_center(&score,f.roi_sum);else if(sample)c3_score_end_frame(&score);
      if(sample){c3_score_end_frame(&ref);assert(score.count==score_points[m]);assert(score.count==ref.count&&score.sum==ref.sum&&score.score==ref.score);}
      else {assert(score.count==0&&score.sum==0&&score.score==ref.score);}
      ++frames;
   }
 }
 c3_settings_t s;c3_settings_defaults(&s);for(unsigned a=0;a<4;a++){s.peak_area=a;c3_settings_record_t r;c3_settings_make_record(&r,&s,17);assert(c3_settings_record_valid(&r)&&r.data.peak_area==a);}
 s.peak_area=4;assert(!c3_settings_valid(&s));
 printf("PASS: %u frames / %u rows, 4 peak areas x 5 score modes x 8 overlay configs x 5 patterns; dense legacy exact pixels/ROI/threshold, sparse oracle, score sum/count/smoothing, actual fast FIFO helpers and canaries; all 65536 luma/grid values; v6 settings\n",frames,rows);
 return 0;
}

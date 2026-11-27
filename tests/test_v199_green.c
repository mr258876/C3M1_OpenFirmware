/* Dense exact-output regression vs v190 renderers; sparse independent oracle;
 * production FIFO row helpers, real score path & shared score cadence. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_peak_experiment.h"
#include "c3_preview_peak_center.h"
#include "c3_preview_dma_render.h"
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
static unsigned peak_lum(uint16_t p,unsigned sparse){return (C3_FOCUS_GREEN || ((C3_PEAK_EXPERIMENT==3||C3_PEAK_EXPERIMENT==4)&&!sparse)) ? (p>>5)&63u : lum(p);}
static unsigned edge(unsigned x,unsigned y,unsigned sp){int a=peak_lum(image[y][x],sp);return (unsigned)(__builtin_abs(a-(int)peak_lum(image[y][x-1],sp))+__builtin_abs(a-(int)peak_lum(image[y-1][x],sp)));}
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
static void oracle(uint8_t *out,unsigned y,unsigned area,unsigned phase,unsigned mono,unsigned grid,unsigned corners,const uint8_t *mask,unsigned threshold,uint16_t color,uint32_t *sum){
 unsigned center=area==0||area==2,sp=area>=2,step=C3_PEAK_SP_FIXED?2:4;
 unsigned y0=center?60:24,y1=center?180:216,x0=center?80:(sp?2:1),x1=center?240:320;
 for(unsigned x=0;x<320;x++){
    uint16_t c=image[y][x];if(mono)c=c3_effect_gray565(c);
    if(y>=y0&&y<y1&&x>=x0&&x<x1){
       unsigned ax=sp?(x&~1u):x,ay=y;
       if(sp&&y%step!=phase){if(y && (y-1)%step==phase && y-1>=y0)ay=y-1;else ay=0;}
       if(ay>=y0){unsigned e=edge(ax,ay,sp);
           if(ax==x&&ay==y&&x>=80&&x<240&&y>=60&&y<180)*sum+=e;
           if(peak_lum(image[ay][ax],sp)>3&&e>=threshold)c=color;
       }
    }
    if(grid&&(y==80||y==160||x==106||x==213))c=c3_effect_grid565(c);
    if(corners&&c3_focus_roi_corner(x,y))c=0x07e0;
    if(mask)c=c3_effect_dim565(c);
    if(c3_effect_glyph(mask,x))c=0xffff;
    c3_effect_put565(out,x,c);
 }
}
static void render(uint8_t *out,unsigned y,unsigned area,c3_focus_t *f,unsigned threshold,uint16_t color,unsigned mono,unsigned grid,unsigned corners,const uint8_t *mask,c3_score_t *score,unsigned sample,unsigned shared,c3_peak_exp_t *exp){
 cursor=0;
 unsigned score_row=sample&&!shared&&c3_score_row_active(score->mode,y);
 if(!c3_peak_exp_row_active(area,y,exp->phase)&&!score_row&&!mono){
    if(mask){c3_row_copy_osd(out,mask);if(grid)c3_peak_fast_dimmed_grid(out,mask);}
    else {c3_row_copy_plain(out);if(grid)c3_effect_grid_row(out,y,0);}
    if(corners)c3_effect_score_corners(out,y);
 }else{
    c3_row_copy_plain(out);
    if(score_row)c3_score_sample_row(score,y,out);
    c3_peak_exp_pixels(out,y,area,f,threshold,color,mono,shared&&c3_peak_exp_shared_accum(area,score->mode)?score:NULL,exp);
    c3_peak_fast_overlay(out,y,grid,corners,mask);
 }
 assert(cursor==640);
}
int main(void){
 unsigned frames=0,rows=0;
 static const unsigned score_points[]={0,19200,1200,76241,4800};
 const unsigned step=C3_PEAK_SP_FIXED?2:4;
 for(unsigned a=0;a<4;a++){
   unsigned center=(a==0||a==2),sp=(a>=2),y0=center?60:24,y1=center?180:216;
   unsigned covered[240]={0};c3_peak_exp_t exp={0};
   for(unsigned k=0;k<4;k++){
     c3_peak_exp_begin(&exp,a,1);
     unsigned phase=C3_PEAK_SP_FIXED?0:((k&1)*2+(k>>1));
     if(sp)assert(exp.phase==phase);
     unsigned active=0,n=0;
     for(unsigned y=0;y<240;y++){
       unsigned ref=0;
       for(unsigned r=y0;r<y1;r++)if(!sp||r%step==phase){
         if(y==r||y==r-1||(sp&&y==r+1&&y<y1))ref=1;
         if(y==r){n+=center?(sp?80:160):(sp?159:319);covered[y]++;}
       }
       assert(c3_peak_exp_row_active(a,y,exp.phase)==ref);active+=ref;
     }
     assert(n==(sp?((y1-y0)/step)*(center?80:159):(y1-y0)*(center?160:319)));
     assert(c3_peak_exp_roi_count(a)==(sp?19200/(2*step):19200));
     printf("area=%u phase=%u staged_rows=%u gradients=%u\n",a,exp.phase,active,n);
   }
   for(unsigned y=y0;y<y1;y++)assert(covered[y]==(!sp?4:step==4?1:y%2==0?4:0));
   c3_peak_exp_begin(&exp,a,0);c3_peak_exp_begin(&exp,a,1);assert(exp.phase==0&&exp.count==0);
 }
 for(unsigned i=0;i<65536;i++){
    uint8_t p[2]={(uint8_t)(i>>8),(uint8_t)i};assert(c3_luma_at(p)==lum(i));assert(c3_green_luma(p)==((i>>5)&63u));assert(c3_peak_dense_luma(p)==peak_lum(i,0));
    /* Exact grid-on-dim order over every RGB565 value. */
    assert(c3_effect_dim565(c3_effect_grid565(i))==c3_effect_dim565((uint16_t)(c3_effect_dim565(i)+0x7befu)));
 }
 for(unsigned a=0;a<4;a++)for(unsigned m=0;m<5;m++)for(unsigned cfg=0;cfg<8;cfg++){
   unsigned mono=cfg&1,grid=(cfg>>1)&1,osd=(cfg>>2)&1;
   c3_focus_t f,old;c3_focus_reset(&f);c3_focus_reset(&old);
   c3_score_t score,ref;c3_score_reset(&score);c3_score_reset(&ref);
   unsigned shared=c3_peak_exp_shared(a,m),corners=c3_score_is_center(m);c3_peak_exp_t exp={0}, dexp={0};
   assert(shared==((m==C3_SCORE_CENTER_SPARSE && (a>=2 || C3_FOCUS_GREEN)) || (C3_FOCUS_GREEN && a<2 && m==C3_SCORE_CENTER)));
   c3_focus_t df;c3_score_t ds;c3_focus_reset(&df);c3_score_reset(&ds);
   for(unsigned k=0;k<5;k++){
      c3_peak_exp_begin(&exp,a,1);pattern(k);uint16_t color=(uint16_t)(0xf800u^(cfg*0x421u));
      unsigned sample=c3_score_schedule_frame(&score,m,shared);
      assert(sample==c3_score_schedule_frame(&ds,m,shared));c3_peak_exp_begin(&dexp,a,1);c3_focus_begin_frame(&df);
      c3_live_render_t params={grid,mono,1,corners,sample,shared,m,a,f.threshold,color};
      uint32_t oracle_score_sum=0,oracle_count=0;
      c3_score_begin_frame(&ref,m);
      uint8_t threshold=f.threshold;
      assert((C3_PEAK_EXPERIMENT==3||C3_PEAK_EXPERIMENT==4)||a>=2||f.threshold==old.threshold);
      c3_focus_begin_frame(&f);c3_focus_begin_frame(&old);uint32_t sum=0;
      for(unsigned y=0;y<240;y++){
        _Alignas(4) uint8_t outbuf[672],b[640],legacy[640],bits[40];
        uint8_t *out=outbuf+16;memset(outbuf,0xa5,sizeof(outbuf));
        for(unsigned x=0;x<320;x++)c3_effect_put565(raw,x,image[y][x]);
        for(unsigned j=0;j<40;j++)bits[j]=j%3==0?0:(uint8_t)rnd();
        const uint8_t *mask=osd&&(y<24||y>=216)?bits:NULL;
        if(sample){
          c3_score_sample_row(&ref,y,raw);
          unsigned center=c3_score_is_center(m), step=(m==C3_SCORE_CENTER_SPARSE||m==C3_SCORE_FULL_SPARSE)?4:1;
          unsigned x0=center?80:1,x1=center?240:320,y0=center?60:1,y1=center?180:240;
          if(y>=y0&&y<y1&&(y-y0)%step==0)for(unsigned x=x0;x<x1;x+=step){
            unsigned v=C3_FOCUS_GREEN?((image[y][x]>>5)&63):lum(image[y][x]);
            unsigned l=C3_FOCUS_GREEN?((image[y][x-1]>>5)&63):lum(image[y][x-1]);
            unsigned u=C3_FOCUS_GREEN?((image[y-1][x]>>5)&63):lum(image[y-1][x]);
            oracle_score_sum+=__builtin_abs((int)v-(int)l)+__builtin_abs((int)v-(int)u);oracle_count++;
          }
        }
        oracle(b,y,a,exp.phase,mono,grid,corners,mask,threshold,color,&sum);
        render(out,y,a,&f,threshold,color,mono,grid,corners,mask,&score,sample,shared,&exp);
        assert(!memcmp(out,b,640));
        uint8_t dma_row[640];memcpy(dma_row,raw,640);
        c3_live_render_row(dma_row,y,mask,&params,&df,&ds,&dexp);assert(!memcmp(dma_row,out,640));
        if(a<2&&C3_PEAK_EXPERIMENT!=3&&C3_PEAK_EXPERIMENT!=4){memcpy(legacy,raw,640);if(a==0)c3_effect_peak_line_center(legacy,y,&old,threshold,color,mono,grid,corners,mask);else c3_effect_peak_line_staged(legacy,y,&old,threshold,color,mono,grid,corners,mask);assert(!memcmp(out,legacy,640));assert(f.roi_sum==old.roi_sum);}
        for(unsigned j=0;j<16;j++)assert(outbuf[j]==0xa5&&outbuf[656+j]==0xa5);
        ++rows;
      }
      assert(f.roi_sum==sum&&df.roi_sum==sum);
      c3_focus_end_frame_area(&df,c3_peak_exp_roi_count(a));
      if(shared&&!c3_peak_exp_shared_accum(a,m))c3_score_end_shared_center(&ds,df.roi_sum);else if(sample)c3_score_end_frame(&ds);
      c3_focus_end_frame_area(&f,c3_peak_exp_roi_count(a));
      if(a<2&&C3_PEAK_EXPERIMENT!=3&&C3_PEAK_EXPERIMENT!=4){c3_focus_end_frame(&old);assert(f.score==old.score&&f.threshold==old.threshold);}
      if(shared&&!c3_peak_exp_shared_accum(a,m))c3_score_end_shared_center(&score,f.roi_sum);else if(sample)c3_score_end_frame(&score);
      if(sample){c3_score_end_frame(&ref);assert(score.count==score_points[m]);assert(score.count==ref.count&&score.sum==ref.sum&&score.score==ref.score);assert(score.count==oracle_count&&score.sum==oracle_score_sum);}
      else {assert(score.count==0&&score.sum==0&&score.score==ref.score);}
      assert(df.threshold==f.threshold&&ds.score==score.score&&ds.sum==score.sum&&ds.count==score.count);++frames;
   }
 }
 c3_settings_t s;c3_settings_defaults(&s);for(unsigned a=0;a<4;a++){s.peak_area=a;c3_settings_record_t r;c3_settings_make_record(&r,&s,17);assert(c3_settings_record_valid(&r)&&r.data.peak_area==a);}
 s.peak_area=4;assert(!c3_settings_valid(&s));
 printf("PASS v199: %u frames / %u rows, 4 peak areas x 5 score modes x 8 overlay configs x 5 patterns; dense legacy exact pixels/ROI/threshold except FAST green oracle, sparse expansion/phase oracle, score sum/count/smoothing, actual fast FIFO helpers and canaries; all 65536 luma/grid values; v6 settings\n",frames,rows);
 return 0;
}

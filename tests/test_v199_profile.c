/* Test actual OSD glyph output, independent values and fixed clock budget. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../C3M1_Camera/Core/Src/c3_osd.c"
c3_live_profile_t c3_live_profile;
static void prep(void){c3_osd_prepare(123,1,0,0,1,1,24,1,272,1,1,1,1,0,1,0,0,NULL);}
int main(void){
    assert(c3_live_slack(10000,35840)==25840);
    assert(c3_live_slack(35840,35840)==0);assert(c3_live_slack(40000,35840)==0);
    for(unsigned i=0;i<100000;i++){unsigned b=35840;assert(c3_live_slack(i,b)==(i<b?b-i:0));}
    c3_live_profile=(c3_live_profile_t){.unpack=7200*12,.peak=7200*123,.score=0,.cpu=7200*160,.wait=7200*130,.lcd=7200*15,.draw=7200*345,.max_block=72*450,.max_overlap=72*499,.over=2,.overlap=59,.max_peak_overlap=72*420,.peak_over=0,.peak_overlap=31,.valid=1,.area=2,.peaking=1,.mode=2,.sample=1,.shared=1};
    prep();uint8_t expected[C3_OSD_BAND_H][C3_OSD_STRIDE];memcpy(expected,c3_osd_mask[1],sizeof expected);
    memset(c3_osd_mask[1],0,sizeof expected);text(8,218,"U01.2 P12.3 S00.0 C16.0 W13.0 L01.5 D34.5",1);text(8,229,"MAX0450 X0499 I0420 O02/59 E00/31 A3 FS2 R1 H1",1);
    assert(memcmp(expected,c3_osd_mask[1],sizeof expected)==0);
    c3_live_profile.sample=0;prep();assert(memcmp(expected,c3_osd_mask[1],sizeof expected)!=0);
    c3_live_profile.valid=0;prep();memcpy(expected,c3_osd_mask[1],sizeof expected);memset(c3_osd_mask[1],0,sizeof expected);text(8,221,"DMA PROFILE WAIT / CPU",1);assert(!memcmp(expected,c3_osd_mask[1],sizeof expected));
    puts("PASS v199: 100000 slack values, equality/over-budget, exact 2-line OSD raster, prior-frame update, invalid data display");
}

/* Actual firmware mask dump for host preview (not camera screenshots). */
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "c3_osd.h"
#include "c3_exposure.h"
#include "c3_settings.h"
int main(void) {
  c3_meter_t m={.exposure_rows=80,.gain_x16=40,.valid=1};
  c3_osd_set_battery(75,1);
  for (unsigned i=0;i<4;++i) {
    unsigned mode=i==0?C3_SHOOT_P:i==3?C3_SHOOT_M:C3_SHOOT_S;
    unsigned z=(i==1 || i==3), shutter=mode==C3_SHOOT_P?C3_EXPOSURE_AUTO_CODE:4;
    unsigned g=mode==C3_SHOOT_M?3:0;
    unsigned sd=i!=3;
    unsigned red=c3_osd_update_card_warning(sd,0);
    c3_osd_prepare(123,sd,1,0,1,i!=2,72,i!=2,273,mode,shutter,g,4,2,mode!=C3_SHOOT_M,z,i==3?C3_OUTPUT_BMP:0,&m);
    for(unsigned y=0;y<240;y++) for(unsigned x=0;x<320;x++) {
      uint16_t v=(y<24||y>=216)?0x2104:0x39E7;
      if(y<24||y>=216){ const uint8_t *r=c3_osd_row(y); if(r[x/8]&(0x80>>(x%8)))v=c3_osd_glyph_color_auto(red,shutter==C3_EXPOSURE_AUTO_CODE,g==0,x,y); }
      if (y>=24 && y<216 && (x==106 || x==213 || y==80 || y==160))v=0x7BEF;
      assert(fwrite(&v,2,1,stdout)==1);
    }
  }
  return 0;
}

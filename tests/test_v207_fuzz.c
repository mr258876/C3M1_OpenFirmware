#include "c3_play_decode.h"
#include "c3_dng.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t buf[C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES+C3_DNG_TAIL_BYTES];
static uint8_t original_head[C3_DNG_HEADER_BYTES],original_tail[C3_DNG_TAIL_BYTES];
static uint32_t state=0x123abcdeu;
static uint32_t rand32(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static uint8_t memread(void *ctx,uint32_t o,uint8_t *d,uint32_t n){uint32_t sz=*(uint32_t *)ctx;assert(o<=sz&&n<=sz-o);memcpy(d,buf+o,n);return 1;}
int main(void){
 c3_dng_begin_shot();c3_dng_make_header(original_head);c3_dng_make_tail(original_tail);
 for(unsigned i=C3_DNG_HEADER_BYTES;i<C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES;i++)buf[i]=(uint8_t)rand32();
 unsigned passed=0;uint8_t row[640];c3_play_image_t im;
 for(unsigned test=0;test<10000;test++){
  memcpy(buf,original_head,sizeof original_head);memcpy(buf+sizeof buf-sizeof original_tail,original_tail,sizeof original_tail);
  for(unsigned j=0;j<1+test%6;j++){
   unsigned o=rand32()%(sizeof original_head+sizeof original_tail);if(o>=sizeof original_head)o+=C3_DNG_STRIP_BYTES;
   buf[o]=(uint8_t)rand32();
  }
  uint32_t sz=sizeof buf;if(test%13==0)sz=rand32()%sizeof buf;
  c3_play_file_t f={memread,&sz,sz};
  if(c3_play_parse(&f,1,&im)){
   for(unsigned k=0;k<2;k++){uint16_t y=k?239:0;uint32_t o=c3_play_row_offset(&im,y);assert(o<=sz&&im.row_bytes<=sz-o);c3_play_render_row(&im,buf+o,row);}
   passed++;
  }
 }
 printf("PASS 10000 randomized TIFF metadata/length mutations under ASan/UBSan (%u accepted, rendered first/last rows)\n",passed);
}

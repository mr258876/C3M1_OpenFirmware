#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "c3_dng.h"
int main(int argc,char **argv)
{
    if(argc!=2) return 1;
    FILE *f=fopen(argv[1],"wb");
    if(!f) return 2;
    uint8_t header[C3_DNG_HEADER_BYTES];
    c3_dng_make_header(header);
    fwrite(header,1,sizeof(header),f);
    for(uint32_t j=0;j<C3_RAW_HEIGHT;j++) {
        for(uint32_t i=0;i<C3_RAW_WIDTH;i++) {
            fputc((int)((i+j)&255u),f);
        }
    }
#if C3_DNG_SOFT_PROFILE
    uint8_t tail[C3_DNG_TAIL_BYTES];
    c3_dng_make_tail(tail);
    fwrite(tail,1,sizeof(tail),f);
#endif
    fclose(f); return 0;
}

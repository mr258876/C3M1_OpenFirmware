#include "c3_play_decode.h"
#include <string.h>

static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint8_t rd(const c3_play_file_t *f,uint32_t at,uint8_t *p,uint32_t n)
{return at<=f->size && n<=f->size-at && f->read(f->ctx,at,p,n);}
static char upper(char c){return c>='a'&&c<='z'?(char)(c-32):c;}
uint32_t c3_play_name_key(const char *s)
{
    if(!s || strlen(s)!=12u || upper(s[0])!='C' || s[1]!='3' || s[2]!='_' || s[8]!='.')return 0;
    uint32_t n=0;
    for(unsigned i=3;i<8;i++){if(s[i]<'0'||s[i]>'9')return 0;n=n*10u+(uint32_t)(s[i]-'0');}
    if(!n)return 0;
    if(upper(s[9])=='D'&&upper(s[10])=='N'&&upper(s[11])=='G')return n*2u+1u;
    if(upper(s[9])=='B'&&upper(s[10])=='M'&&upper(s[11])=='P')return n*2u;
    return 0;
}
void c3_play_path(uint32_t key,char path[16])
{
    memcpy(path,"0:/C3_00000.DNG",16u);
    uint32_t n=key/2u;
    for(int i=10;i>=6;i--){path[i]=(char)('0'+n%10u);n/=10u;}
    if(!(key&1u)){path[12]='B';path[13]='M';path[14]='P';}
}
/* Q-format unsigned ratio without 64-bit division or overflow on malformed
 * 32-bit TIFF rationals. Return false if above limit (including fraction). */
static uint8_t ratio(uint32_t n,uint32_t d,unsigned frac,uint32_t max,uint32_t *out)
{
    if(!d)return 0;
    uint32_t q=n/d,r=n%d;
    if(q>max>>frac)return 0;
    while(frac--){q<<=1;if(r>=d-r){r-=d-r;q|=1u;}else r+=r;}
    if(q>max)return 0;
    *out=q;return 1;
}
typedef struct{uint32_t value,count;uint16_t type;} tag_t;
static uint8_t scalar(tag_t t,uint32_t *v)
{
    if(t.count!=1u)return 0;
    if(t.type==3u)*v=t.value&65535u;
    else if(t.type==4u)*v=t.value;
    else return 0;
    return 1;
}
static uint8_t number(const c3_play_file_t *f,tag_t t,unsigned i,unsigned frac,uint32_t limit,uint32_t *out)
{
    uint32_t n,d=1u,bytes;uint8_t b[8];
    if(i>=t.count)return 0;
    if(t.type==3u)bytes=2u;else if(t.type==4u)bytes=4u;else if(t.type==5u)bytes=8u;else return 0;
    if(t.count<=4u/bytes){n=bytes==2u?(t.value>>(16u*i))&65535u:t.value;}
    else {
        if(t.value>f->size || t.count>(f->size-t.value)/bytes || !rd(f,t.value+i*bytes,b,bytes))return 0;
        n=bytes==2u?le16(b):le32(b);if(bytes==8u)d=le32(b+4);
    }
    return ratio(n,d,frac,limit,out);
}
static uint8_t matrix(const c3_play_file_t *f,tag_t t,int32_t m[9])
{
    /* ForwardMatrix is WB camera RGB -> XYZ D50. Use D50-adapted sRGB
     * transform, not a second application of ColorMatrix1. */
    static const int32_t xyz_to_rgb[9]={12836,-6623,-2010,-4009,7849,137,295,-938,5756};
    if(!t.count){for(unsigned i=0;i<9;i++)m[i]=(i%4u)==0u?4096:0;return 1;}
    if(t.type!=10u||t.count!=9u||t.value>f->size||72u>f->size-t.value)return 0;
    int32_t fw[9];uint8_t b[8];
    for(unsigned i=0;i<9;i++){
        if(!rd(f,t.value+i*8u,b,8))return 0;
        uint32_t n=le32(b),d=le32(b+4),q;uint8_t neg=(uint8_t)(n>>31);
        if(d>0x7fffffffu||!ratio(neg?0u-n:n,d,12u,16384u,&q))return 0;
        fw[i]=neg?-(int32_t)q:(int32_t)q;
    }
    for(unsigned y=0;y<3;y++)for(unsigned x=0;x<3;x++){
        int32_t a=0;for(unsigned k=0;k<3;k++)a+=xyz_to_rgb[y*3+k]*fw[k*3+x];
        m[y*3+x]=a/4096;
        if(m[y*3+x] < -16384 || m[y*3+x] > 16384)return 0;
    }
    return 1;
}
static uint8_t parse_bmp(const c3_play_file_t *f,c3_play_image_t *im)
{
    uint8_t h[54];if(!rd(f,0,h,sizeof h)||h[0]!='B'||h[1]!='M')return 0;
    uint32_t off=le32(h+10),dib=le32(h+14),height=le32(h+22);
    if(dib<40u || off<14u || dib>off-14u || le32(h+18)!=320u ||
       (height!=240u&&height!=(uint32_t)-240)||le16(h+26)!=1u||le16(h+28)!=24u||le32(h+30)!=0u||
       off>f->size||230400u>f->size-off)return 0;
    im->raw=0;im->width=320;im->row_bytes=960;im->offset=off;im->reverse_y=height==240u;
    return 1;
}
static uint8_t parse_dng(const c3_play_file_t *f,c3_play_image_t *im)
{
    /* IFD0, little endian, single uncompressed CFA strip. Bound all entry
     * counts/offsets before use. Reject arbitrary TIFF/JPEG/SubIFD layouts. */
    uint8_t h[12];if(!rd(f,0,h,8)||h[0]!='I'||h[1]!='I'||le16(h+2)!=42u)return 0;
    uint32_t ifd=le32(h+4);if(ifd<8u||!rd(f,ifd,h,2))return 0;
    uint16_t count=le16(h);if(!count||count>64u||ifd>f->size-2u||12u*count+4u>f->size-ifd-2u)return 0;
    uint32_t w=0,height=0,bits=0,compress=0,photo=0,off=0,orient=1,samples=1,rows=0,bytes=0,planar=1,white=0;
    uint8_t cfa_dim=0,cfa=0,version=0;
    tag_t black={0},repeat={0},neutral={0},forward={0};
    uint16_t last=0;
    for(unsigned i=0;i<count;i++){
        if(!rd(f,ifd+2u+12u*i,h,12))return 0;
        uint16_t id=le16(h);tag_t t={le32(h+8),le32(h+4),le16(h+2)};
        if(i&&id<=last)return 0;
        last=id; /* generated IFDs are sorted/unique */
        uint32_t *v=0;
        switch(id){
        case 256:v=&w;break;case 257:v=&height;break;case 258:v=&bits;break;
        case 259:v=&compress;break;case 262:v=&photo;break;case 273:v=&off;break;
        case 274:v=&orient;break;case 277:v=&samples;break;case 278:v=&rows;break;
        case 279:v=&bytes;break;case 284:v=&planar;break;case 50717:v=&white;break;
        case 33421:cfa_dim=(uint8_t)(t.type==3u&&t.count==2u&&t.value==0x00020002u);break;
        case 33422:cfa=(uint8_t)(t.type==1u&&t.count==4u&&t.value==0x00010102u);break;
        case 50706:version=(uint8_t)(t.type==1u&&t.count==4u&&(t.value&255u)==1u);break;
        case 50713:repeat=t;break;case 50714:black=t;break;case 50728:neutral=t;break;case 50964:forward=t;break;
        default:break;
        }
        if(v&&!scalar(t,v))return 0;
    }
    if((w!=640u&&w!=642u)||height!=480u||(bits!=8u&&bits!=16u)||compress!=1u||photo!=32803u||
       samples!=1u||rows<height||planar!=1u||(orient!=1u&&orient!=3u)||!cfa_dim||!cfa||!version||
       off<ifd+2u+12u*count+4u||off>f->size||bytes!=w*height*(bits/8u)||bytes>f->size-off)return 0;
    if(!white)white=bits==8u?255u:65535u;
    if(white>(bits==8u?255u:65535u))return 0;
    if(black.count!=0u&&black.count!=1u&&black.count!=4u)return 0;
    if(repeat.count && (repeat.type!=3u || repeat.count!=2u ||
        repeat.value!=(black.count==4u?0x00020002u:0x00010001u)))return 0;
    if(black.count==4u && !repeat.count)return 0;
    uint32_t wb[3]={4096u,4096u,4096u};
    if(neutral.count){
        uint32_t n[3];if(neutral.count!=3u||neutral.type!=5u)return 0;
        for(unsigned i=0;i<3;i++)if(!number(f,neutral,i,12u,32768u,&n[i])||!n[i])return 0;
        for(unsigned i=0;i<3;i++)if(!ratio(n[1],n[i],12u,32768u,&wb[i]))return 0;
    }
    if(!matrix(f,forward,im->matrix))return 0;
    static const uint8_t channel[4]={2,1,1,0};
    for(unsigned c=0;c<4;c++){
        uint32_t bl=0;if(black.count&&!number(f,black,black.count==4u?c:0u,8u,65535u<<8u,&bl))return 0;
        if(bl>=white*256u)return 0;
        uint32_t range=white*256u-bl;
        for(unsigned v=0;v<256;v++){
            /* ACR16 stores RAW8 << 8. Quantize to the same 8-bit input
             * precision for preview only; never modify file pixels. */
            uint32_t s=v*(bits==16u?65536u:256u),q=0;
            if(s>bl){uint32_t d=s-bl;if(d>=range)q=4096u;else if(!ratio(d,range,12u,4096u,&q))return 0;}
            im->lut[c][v]=(uint16_t)((q*wb[channel[c]]+2048u)>>12u);
        }
    }
    im->raw=1;im->sample_bytes=(uint8_t)(bits/8u);im->width=(uint16_t)w;im->offset=off;
    im->row_bytes=w*2u*im->sample_bytes;im->reverse_x=im->reverse_y=(uint8_t)(orient==3u);
    return 1;
}
uint8_t c3_play_parse(const c3_play_file_t *f,uint8_t dng,c3_play_image_t *im)
{
    if(!f||!f->read||!im)return 0;
    memset(im,0,sizeof *im);return dng?parse_dng(f,im):parse_bmp(f,im);
}
uint16_t c3_play_display_y(const c3_play_image_t *im,uint16_t y){return im->reverse_y?(uint16_t)(239u-y):y;}
uint32_t c3_play_row_offset(const c3_play_image_t *im,uint16_t y){return im->offset+(uint32_t)y*im->row_bytes;}
static const uint8_t srgb[256]={
    0,13,22,28,34,38,42,46,50,53,56,59,61,64,66,69,
    71,73,75,77,79,81,83,85,86,88,90,92,93,95,96,98,
    99,101,102,104,105,106,108,109,110,112,113,114,115,117,118,119,
    120,121,122,124,125,126,127,128,129,130,131,132,133,134,135,136,
    137,138,139,140,141,142,143,144,145,146,147,148,148,149,150,151,
    152,153,154,155,155,156,157,158,159,159,160,161,162,163,163,164,
    165,166,167,167,168,169,170,170,171,172,173,173,174,175,175,176,
    177,178,178,179,180,180,181,182,182,183,184,185,185,186,187,187,
    188,189,189,190,190,191,192,192,193,194,194,195,196,196,197,197,
    198,199,199,200,200,201,202,202,203,203,204,205,205,206,206,207,
    208,208,209,209,210,210,211,212,212,213,213,214,214,215,215,216,
    216,217,218,218,219,219,220,220,221,221,222,222,223,223,224,224,
    225,226,226,227,227,228,228,229,229,230,230,231,231,232,232,233,
    233,234,234,235,235,236,236,237,237,238,238,238,239,239,240,240,
    241,241,242,242,243,243,244,244,245,245,246,246,246,247,247,248,
    248,249,249,250,250,251,251,251,252,252,253,253,254,254,255,255,
};
static uint8_t display(int32_t q){if(q<=0)return 0;if(q>=4096)return 255;return srgb[(uint32_t)(q*255+2048)>>12u];}
static void put(uint8_t *out,unsigned x,uint8_t r,uint8_t g,uint8_t b)
{
    uint16_t v=(uint16_t)(((r&248u)<<8)|((g&252u)<<3)|(b>>3));out[2u*x]=(uint8_t)(v>>8);out[2u*x+1u]=(uint8_t)v;
}
void c3_play_render_row(const c3_play_image_t *im,const uint8_t *row,uint8_t out[640])
{
    if(!im->raw){for(unsigned x=0;x<320;x++)put(out,x,row[3*x+2],row[3*x+1],row[3*x]);return;}
    const unsigned b=im->sample_bytes,hi=b-1u;
    const uint8_t *a=row+hi,*z=a+(unsigned)im->width*b;
    const int32_t *m=im->matrix;
    for(unsigned x=0;x<320;x++,a+=2u*b,z+=2u*b){
        int32_t blue=im->lut[0][a[0]],green=(im->lut[1][a[b]]+im->lut[2][z[0]]+1u)/2u,red=im->lut[3][z[b]];
        uint8_t r=display((m[0]*red+m[1]*green+m[2]*blue)/4096),
                g=display((m[3]*red+m[4]*green+m[5]*blue)/4096),
                bl=display((m[6]*red+m[7]*green+m[8]*blue)/4096);
        put(out,im->reverse_x?319u-x:x,r,g,bl);
    }
}

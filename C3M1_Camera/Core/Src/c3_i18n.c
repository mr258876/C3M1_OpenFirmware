#include "c3_i18n.h"
#include "c3_font.h"
#include "c3_i18n_data.inc"
static uint8_t use_zh;
void c3_i18n_set_language(uint8_t chinese){use_zh=!!chinese;}
uint8_t c3_i18n_is_chinese(void){return use_zh;}
static int text_equal(const char *a,const char *b)
{
    while(*a && *a==*b){a++;b++;}
    return *a==*b;
}
const char *c3_i18n_translate(const char *key)
{
    if (!key) return "";
    if (!use_zh) return key;
    for (unsigned i=0;i<sizeof(translations)/sizeof(translations[0]);i++)
        if (text_equal(key,translations[i].en)) return translations[i].zh;
    return key;
}
/* Decode only the codepoints needed for static firmware strings. Malformed
 * sequences consume one byte and show a fallback glyph, never overread. */
static uint16_t next_cp(const char **text)
{
    const unsigned char *p=(const unsigned char *)*text;
    uint16_t cp=*p++;
    if(cp>=0xE0u && cp<=0xEFu && (p[0]&0xC0u)==0x80u && (p[1]&0xC0u)==0x80u){
        cp=(uint16_t)(((cp&15u)<<12)|((p[0]&63u)<<6)|(p[1]&63u));p+=2;
    }else if(cp>=0xC2u && cp<=0xDFu && (p[0]&0xC0u)==0x80u){
        cp=(uint16_t)(((cp&31u)<<6)|(p[0]&63u));p++;
    }else if(cp>=128u){cp=0xFFFDu;}
    *text=(const char *)p;return cp;
}
static const uint16_t *glyph(uint16_t cp)
{
    unsigned lo=0,hi=sizeof(zh_glyphs)/sizeof(zh_glyphs[0]);
    while(lo<hi){unsigned m=lo+(hi-lo)/2;if(zh_glyphs[m].cp<cp)lo=m+1;else hi=m;}
    if(lo<sizeof(zh_glyphs)/sizeof(zh_glyphs[0]) && zh_glyphs[lo].cp==cp)return zh_glyphs[lo].rows;
    return zh_glyphs[0].rows; /* first entry is '?' */
}
int c3_i18n_width(const char *text,int scale)
{
    if(scale<1)scale=1;
    int w=0;while(*text){uint16_t cp=next_cp(&text);w+=cp<128u?6*scale:17;}
    return w;
}
void c3_ui_text(int x,int y,const char *key,uint16_t color,int scale)
{
    const char *s=c3_i18n_translate(key);
    if(s==key || !use_zh){c3_lcd_text(x,y,s,color,scale);return;}
    if(scale<1)scale=1;
    const int top=y+(7*scale-16)/2;
    while(*s){
        uint16_t cp=next_cp(&s);
        if(cp<128u){char c[2]={(char)cp,0};c3_lcd_text(x,y,c,color,scale);x+=6*scale;continue;}
        const uint16_t *bits=glyph(cp);
        for(int r=0;r<16;r++){
            int col=0;while(col<16){
                while(col<16 && !(bits[r]&(0x8000u>>col)))col++;
                int start=col;while(col<16 && (bits[r]&(0x8000u>>col)))col++;
                if(col>start)c3_lcd_rect(x+start,top+r,col-start,1,color);
            }
        }
        x+=17;
    }
}
void c3_ui_message(const char *key)
{
    const char *s=c3_i18n_translate(key);
    if(s==key || !use_zh){c3_lcd_message(key);return;}
    int scale=2,w=c3_i18n_width(s,scale);
    if(w>260){scale=1;w=c3_i18n_width(s,scale);}
    c3_lcd_rect(22,97,276,38,0);
    c3_ui_text((320-w)/2,116-(7*scale)/2,key,0xFFFFu,scale);
}

#include "c3_ui_hints.h"
/* Original 1-bit artwork, one row per word, most significant used bit left.
 * ISO and 4X use the legacy 5x7 font within matching outlined keycaps. */
static const uint32_t dial_icon[14]={0xFC0u,0x3030u,0x4848u,0x8004u,0x1208Au,0x10102u,0x10202u,0x10402u,0x10002u,0x1200Au,0x8004u,0x4848u,0x3030u,0xFC0u};
static const uint32_t shot_icon[14]={0xFC0u,0x3030u,0x4008u,0x8FC4u,0x11022u,0x12012u,0x12012u,0x12012u,0x12012u,0x11022u,0x8FC4u,0x4008u,0x3030u,0xFC0u};
static const uint32_t film_icon[14]={0x0u,0xCu,0x12u,0x22u,0x44u,0x88u,0x110u,0x220u,0x440u,0x3F880u,0x40100u,0x40200u,0x3FC00u,0x0u};
#include "c3_startup_hint.h"
void c3_ui_draw_startup_hint(void)
{
    const char *text=c3_i18n_translate(C3_STARTUP_HINT_KEY);
    int width=8+20+4+c3_i18n_width(text,1)+8;
    if(width>320)return;
    int x=(320-width)/2;
    /* 194..213: above bottom OSD (216..239); no DMA is active here. */
    c3_lcd_window((uint16_t)x,194u,(uint16_t)(x+width-1),213u);
    for(int r=0;r<20;r++){
        for(int c=0;c<8;c++)c3_lcd_pixel_stream(0u);
        for(int c=0;c<20;c++){
            uint8_t set=(uint8_t)(r>=3&&r<17&&((film_icon[r-3]>>(19-c))&1u));
            c3_lcd_pixel_stream(set?0xFFFFu:0u);
        }
        for(int c=0;c<4;c++)c3_lcd_pixel_stream(0u);
        const char *s=text;
        while(*s){
            uint16_t cp=next_cp(&s);
            if(cp<128u){
                const uint8_t *f=c3_font_glyph((char)cp);
                for(int c=0;c<6;c++){
                    uint8_t set=(uint8_t)(f&&c<5&&r>=6&&r<13&&((f[c]>>(r-6))&1u));
                    c3_lcd_pixel_stream(set?0xFFFFu:0u);
                }
            }else{
                const uint16_t *g=glyph(cp);
                for(int c=0;c<17;c++){
                    uint8_t set=(uint8_t)(c<16&&r>=2&&r<18&&((g[r-2]>>(15-c))&1u));
                    c3_lcd_pixel_stream(set?0xFFFFu:0u);
                }
            }
        }
        for(int c=0;c<8;c++)c3_lcd_pixel_stream(0u);
    }
}
static void hint_bitmap(int x,int y,int w,const uint32_t *rows,uint16_t color)
{
    for(int r=0;r<14;r++){
        int c=0;
        while(c<w){
            while(c<w && !(rows[r]&(1u<<(w-1-c))))c++;
            int start=c;
            while(c<w && (rows[r]&(1u<<(w-1-c))))c++;
            if(c>start)c3_lcd_rect(x+start,y+r,c-start,1,color);
        }
    }
}
static void hint_keycap(int x,int y,const char *s,int label_x,uint16_t color)
{
    c3_lcd_rect(x+2,y,20,1,color);
    c3_lcd_rect(x+2,y+13,20,1,color);
    c3_lcd_rect(x+1,y+1,1,1,color);
    c3_lcd_rect(x+22,y+1,1,1,color);
    c3_lcd_rect(x+1,y+12,1,1,color);
    c3_lcd_rect(x+22,y+12,1,1,color);
    c3_lcd_rect(x,y+2,1,10,color);
    c3_lcd_rect(x+23,y+2,1,10,color);
    c3_lcd_text(x+label_x,y+3,s,color,1);
}
void c3_ui_hint(int x,int y,c3_hint_key_t key,const char *action,uint16_t color)
{
    int w;
    switch(key){
      case C3_HINT_DIAL: w=18;hint_bitmap(x,y+1,w,dial_icon,color);break;
      case C3_HINT_SHOT: w=18;hint_bitmap(x,y+1,w,shot_icon,color);break;
      case C3_HINT_ISO: w=24;hint_keycap(x,y+1,"ISO",3,color);break;
      case C3_HINT_4X: w=24;hint_keycap(x,y+1,"4X",6,color);break;
      case C3_HINT_FILM: w=20;hint_bitmap(x,y+1,w,film_icon,color);break;
      default:return;
    }
    c3_ui_text(x+w+4,y+4,action,color,1);
}

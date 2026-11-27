#include "c3_playback.h"
#if C3_PLAYBACK_ENABLE
#include "c3_play_decode.h"
#include "c3_storage.h"
#include "c3_lcd.h"
#include "c3_i18n.h"
#include "c3_ui_hints.h"
#include "c3_lcd_dma.h"
#include "fatfs.h"
#include <string.h>

/* SDIO's existing polling reader uses word accesses. Always read from
 * sector-aligned offsets into a word-aligned cache; never let an odd BMP
 * data offset or 642-byte stride produce an unaligned direct FatFs read. */
#define CACHE_BYTES 2048u
static uint8_t cache[CACHE_BYTES] __attribute__((aligned(4)));
static uint8_t row[C3_PLAY_ROW_MAX] __attribute__((aligned(4)));
static c3_play_image_t image;
typedef struct {FIL *file;uint32_t base,n;uint8_t io_error;} reader_t;
static uint8_t cached_read(void *ctx,uint32_t offset,uint8_t *dst,uint32_t n)
{
    reader_t *r=(reader_t *)ctx;
    if(offset>f_size(r->file)||n>f_size(r->file)-offset)return 0;
    while(n){
        if(!r->n||offset<r->base||offset-r->base>=r->n){
            uint32_t base=offset&~(CACHE_BYTES-1u),want=f_size(r->file)-base;UINT got=0;
            if(want>CACHE_BYTES)want=CACHE_BYTES;
            if((f_tell(r->file)!=base&&f_lseek(r->file,base)!=FR_OK)||
               f_read(r->file,cache,(UINT)want,&got)!=FR_OK||got!=want){r->io_error=1;r->n=0;return 0;}
            r->base=base;r->n=got;
        }
        uint32_t at=offset-r->base,take=r->n-at;if(take>n)take=n;
        memcpy(dst,cache+at,take);dst+=take;offset+=take;n-=take;
    }
    return 1;
}
/* A constant-memory ordered search. Root FAT directory order is NOT photo
 * order, and gaps, mixed formats, and duplicated numbers must work. */
static uint8_t find_photo(uint32_t pivot,int direction,uint32_t *key)
{
    DIR dir;FILINFO info;FRESULT res;
    *key=0;
    if(f_opendir(&dir,"0:/")!=FR_OK)return 0;
    for(;;){
        res=f_readdir(&dir,&info);if(res!=FR_OK||!info.fname[0])break;
        if(info.fattrib&(AM_DIR|AM_VOL))continue;
        uint32_t k=c3_play_name_key(info.fname);if(!k)continue;
        if(direction<0){if(k<pivot&&k>*key)*key=k;}
        else if(k>pivot&&(!*key||k<*key))*key=k;
    }
    FRESULT close=f_closedir(&dir);return (uint8_t)(res==FR_OK&&close==FR_OK);
}
static void bar(const c3_playback_t *s)
{
    c3_lcd_rect(0,0,320,20,0x0000u);
    c3_ui_text(6,3,"PLAY",0xFFFFu,2);
    if(s->key){char path[16];c3_play_path(s->key,path);c3_lcd_text(96,3,path+6,0xFFFFu,2);}
    c3_lcd_rect(0,224,320,16,0x0000u);
#if C3_UI_ICONS
    c3_ui_hint(6,224,C3_HINT_DIAL,"PREV/NEXT",0xFFFFu);
    c3_ui_hint(122,224,C3_HINT_ISO,"INFO",0xFFFFu);
    c3_ui_hint(222,224,C3_HINT_SHOT,"BACK",0xFFFFu);
#else
    c3_ui_text(6,228,"DIAL PREV/NEXT   ISO INFO   SHOT BACK",0xFFFFu,1);
#endif
}
static uint8_t message(const c3_playback_t *s,const char *text,uint8_t ok)
{
    c3_lcd_fill(0x0841u);
#if C3_POSTSHOT
    if(s->info!=2u)bar(s);
#else
    bar(s);
#endif
    c3_ui_message(text);
    if(!ok)c3_storage_invalidate();
    return ok;
}
static uint8_t draw(c3_playback_t *s)
{
    if(!s->key)return message(s,"NO PHOTOS",1);
    char path[16];c3_play_path(s->key,path);FIL file;
    c3_lcd_fill(0x0841u);
#if C3_POSTSHOT
    if(s->info!=2u)bar(s);
#else
    bar(s);
#endif
    c3_ui_message("LOADING");
    FRESULT res=f_open(&file,path,FA_READ);
    if(res!=FR_OK)return message(s,res==FR_NO_FILE?"FILE MISSING":"READ ERROR",res==FR_NO_FILE);
    reader_t r={&file,0,0,0};c3_play_file_t f={cached_read,&r,f_size(&file)};
    uint8_t valid=c3_play_parse(&f,(uint8_t)(s->key&1u),&image);
    if(valid){
        for(uint16_t y=0;y<240u;y++){
            if(!cached_read(&r,c3_play_row_offset(&image,y),row,image.row_bytes)){valid=0;break;}
            uint16_t dest=c3_play_display_y(&image,y);
            /* Each row gets its own window: file order remains sequential
             * even for Orientation=3 / bottom-up BMP. Finish before the
             * next window, so LCD DMA's own row recovery is still valid. */
            c3_lcd_window(0,dest,319,dest);
#if C3_LCD_DMA_ENABLE
            c3_lcd_dma_begin();c3_play_render_row(&image,row,c3_lcd_dma_line(dest));
            c3_lcd_dma_submit(dest);c3_lcd_dma_finish();
#else
            uint8_t out[640];c3_play_render_row(&image,row,out);
            for(unsigned i=0;i<640;i++)c3_lcd_write_byte_fast(out[i]);
#endif
        }
    }
    res=f_close(&file);
    if(res!=FR_OK||r.io_error)return message(s,"READ ERROR",0);
    if(!valid)return message(s,"BAD IMAGE",1);
    if(s->info==1u)bar(s);
    return 1;
}
#if C3_POSTSHOT
uint8_t c3_playback_show_saved(uint32_t number,c3_output_format_t format)
{
    /* info=2 is the private immediate-review mode: no browsing hints. */
    c3_playback_t s={0,2u};
    if(number<1u||number>99999u||
       (format!=C3_OUTPUT_DNG&&format!=C3_OUTPUT_BMP))return message(&s,"BAD IMAGE",1);
    s.key=number*2u+(format==C3_OUTPUT_DNG?1u:0u);
    return draw(&s);
}
#endif
uint8_t c3_playback_enter(c3_playback_t *s)
{
    s->key=0;s->info=1;
    c3_lcd_fill(0x0841u);bar(s);c3_ui_message("READING CARD");
    if(!c3_storage_mount())return message(s,"NO CARD",0);
    if(!find_photo(200000u,-1,&s->key))return message(s,"READ ERROR",0);
    return draw(s);
}
uint8_t c3_playback_step(c3_playback_t *s,int movement)
{
    if(!s->key)return c3_playback_enter(s);
    uint32_t key;
    /* positive dial direction = next newer picture, negative = older.
     * One scan per debounced dial change; do not queue multiple slow loads. */
    if(!find_photo(s->key,movement<0?-1:1,&key))return message(s,"READ ERROR",0);
    if(!key)return 1; /* clamp at either end, never wrap */
    s->key=key;return draw(s);
}
uint8_t c3_playback_toggle_info(c3_playback_t *s)
{
    if(!s->key)return c3_playback_enter(s);
    s->info^=1u;return draw(s);
}
#endif

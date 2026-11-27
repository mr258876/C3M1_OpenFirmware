/* The real playback controller linked against a deliberately strict SD/LCD
 * fake. There are NO write/rename/unlink symbols in the fake. */
#include "c3_playback.h"
#include "c3_i18n.h"
#include "c3_play_decode.h"
#include "c3_ui_nav.h"
#include "c3_board_pins.h"
#include "c3_dng.h"
#include "c3_bmp.h"
#include "fatfs.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
static uint8_t bmp[C3_BMP_FILE_BYTES],dng[C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES+C3_DNG_TAIL_BYTES],bad[32];
static struct {char name[13];uint8_t attr;uint8_t *data;uint32_t size;} files[]={
 {"C3_00005.BMP",0,bmp,sizeof bmp},{"C3_99999.DNG",AM_DIR,0,0},
 {"C3_00003.DNG",0,dng,sizeof dng},{"C3_00008.TMP",0,bmp,sizeof bmp},
 {"C3_00005.DNG",0,dng,sizeof dng},{"C3_00002.BMP",0,bmp,sizeof bmp},
 {"C3_00007.DNG",0,bad,sizeof bad},{"C3_00001.BMP",AM_VOL,0,0},
 {"OTHER.BMP",0,bmp,sizeof bmp}};
#define N (sizeof files/sizeof files[0])
static int card=1,empty,dir_open,file_open,read_count,fail_read,fail_close,fail_dir,missing,invalidate_count,mount_count;
static unsigned rows,bar_count,pending,dest,begin_count,end_count,active_index;
static uint8_t lcd_line[640];static char msg[40];
uint8_t c3_storage_mount(void){assert(!file_open&&!dir_open);mount_count++;return card;}
void c3_storage_invalidate(void){invalidate_count++;}
FRESULT f_opendir(DIR *d,const char *path){assert(!strcmp(path,"0:/")&&!dir_open&&!file_open);if(!card)return FR_DISK_ERR;d->index=0;dir_open=1;return FR_OK;}
FRESULT f_readdir(DIR *d,FILINFO *i){assert(dir_open&&!file_open);if(fail_dir&&d->index==2)return FR_DISK_ERR;if(empty||d->index>=N){i->fname[0]=0;return FR_OK;}memcpy(i->fname,files[d->index].name,13);i->fattrib=files[d->index++].attr;return FR_OK;}
FRESULT f_closedir(DIR *d){(void)d;assert(dir_open);dir_open=0;return FR_OK;}
FRESULT f_open(FIL *f,const char *path,unsigned mode){assert(!dir_open&&!file_open&&mode==FA_READ);if(!card)return FR_DISK_ERR;if(missing)return FR_NO_FILE;for(unsigned i=0;i<N;i++)if(!strcmp(path+3,files[i].name)){assert(!files[i].attr);f->index=i;f->fptr=0;f->fsize=files[i].size;file_open=1;active_index=i;read_count=0;return FR_OK;}return FR_NO_FILE;}
FRESULT f_lseek(FIL *f,uint32_t pos){assert(file_open&&pos<=f->fsize&&pos%512u==0u);f->fptr=pos;return FR_OK;}
FRESULT f_read(FIL *f,void *dst,UINT want,UINT *got){assert(file_open&&((uintptr_t)dst%4u)==0u&&f->fptr%512u==0u);assert(want<=2048&&want<=f->fsize-f->fptr);read_count++;if(fail_read==read_count){*got=0;return FR_DISK_ERR;}memcpy(dst,files[f->index].data+f->fptr,want);f->fptr+=want;*got=want;return FR_OK;}
FRESULT f_close(FIL *f){(void)f;assert(file_open);file_open=0;return fail_close?FR_DISK_ERR:FR_OK;}
void c3_lcd_fill(uint16_t c){(void)c;assert(!pending);rows=0;bar_count=0;msg[0]=0;}
void c3_lcd_rect(int x,int y,int w,int h,uint16_t c){(void)c;assert(!pending);assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240);}
void c3_lcd_text(int x,int y,const char *s,uint16_t c,int scale){(void)x;(void)y;(void)c;(void)scale;if(!strcmp(s,"PLAY"))bar_count++;}
void c3_lcd_message(const char *s){assert(!pending);strcpy(msg,s);}
void c3_lcd_window(uint16_t x,uint16_t y,uint16_t x1,uint16_t y1){assert(!pending&&x==0&&x1==319&&y==y1&&y<240);dest=y;}
void c3_lcd_dma_begin(void){assert(!pending);pending=1;begin_count++;}
uint8_t *c3_lcd_dma_line(uint16_t y){assert(pending==1&&y==dest);return lcd_line;}
void c3_lcd_dma_submit(uint16_t y){assert(pending==1&&y==dest&&rows<240&&file_open);
 assert(y==(files[active_index].size==sizeof dng?239u-rows:rows));pending=2;rows++;}
void c3_lcd_dma_finish(void){assert(pending==2);pending=0;end_count++;}
static void clean(void){assert(!pending&&!file_open&&!dir_open&&begin_count==end_count);}
static void test_play(void){
 c3_bmp_make_header(bmp);for(unsigned i=C3_BMP_HEADER_BYTES;i<sizeof bmp;i++)bmp[i]=(uint8_t)i;
 c3_dng_begin_shot();c3_dng_make_header(dng);for(unsigned i=C3_DNG_HEADER_BYTES;i<C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES;i++)dng[i]=(uint8_t)i;
 c3_dng_make_tail(dng+C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES);
 c3_playback_t s={0};assert(c3_playback_enter(&s)&&s.key==15&&s.info&&strcmp(msg,"BAD IMAGE")==0);clean();
 assert(c3_playback_step(&s,-1)&&s.key==11&&rows==240);clean();
 assert(c3_playback_toggle_info(&s)&&!s.info&&rows==240&&bar_count==1);clean();
 assert(c3_playback_step(&s,-1)&&s.key==10&&rows==240);clean();
 assert(c3_playback_step(&s,-1)&&s.key==7&&rows==240);clean();
 assert(c3_playback_step(&s,-1)&&s.key==4&&rows==240);clean();
 unsigned before=begin_count;assert(c3_playback_step(&s,-1)&&s.key==4&&before==begin_count);clean();
 assert(c3_playback_step(&s,1)&&s.key==7&&rows==240);clean();
 fail_read=4;int inv=invalidate_count;assert(!c3_playback_toggle_info(&s)&&invalidate_count==inv+1&&!strcmp(msg,"READ ERROR"));clean();fail_read=0;
 fail_close=1;assert(!c3_playback_toggle_info(&s)&&!strcmp(msg,"READ ERROR"));clean();fail_close=0;
 missing=1;assert(c3_playback_toggle_info(&s)&&!strcmp(msg,"FILE MISSING"));clean();missing=0;
 fail_dir=1;assert(!c3_playback_step(&s,1)&&!strcmp(msg,"READ ERROR"));clean();fail_dir=0;
 card=0;assert(!c3_playback_enter(&s)&&s.key==0&&!strcmp(msg,"NO CARD"));clean();
 card=1;empty=1;assert(c3_playback_enter(&s)&&s.key==0&&!strcmp(msg,"NO PHOTOS"));clean();
 empty=0;assert(c3_playback_step(&s,1)&&s.key==15&&!strcmp(msg,"BAD IMAGE"));clean();
 printf("PASS playback controller: mixed unsorted directory, gaps, duplicate IDs, ignored TMP/dir/volume, bad/missing file, no-card/empty, read/close/dir fault, %u LCD rows balanced, sector-aligned cache\n",begin_count);
}
static c3_button_events_t press(uint16_t mask){c3_button_events_t e={0};e.pressed=mask;return e;}
#if C3_POSTSHOT
static void test_immediate(void){
 /* Directory contains newer/corrupt photos and both extensions for ID 5.
    Immediate review must not scan or choose the other format. */
 int mounts=mount_count;fail_dir=1;
 assert(c3_playback_show_saved(5,C3_OUTPUT_DNG)&&active_index==4&&rows==240&&bar_count==0);clean();
 assert(c3_playback_show_saved(5,C3_OUTPUT_BMP)&&active_index==0&&rows==240&&bar_count==0);clean();
 assert(c3_playback_show_saved(3,C3_OUTPUT_DNG)&&active_index==2&&rows==240);clean();
 assert(c3_playback_show_saved(7,C3_OUTPUT_DNG)&&!strcmp(msg,"BAD IMAGE")&&bar_count==0);clean();
 assert(c3_playback_show_saved(99999,C3_OUTPUT_BMP)&&!strcmp(msg,"FILE MISSING"));clean();
 assert(c3_playback_show_saved(0,C3_OUTPUT_BMP)&&!strcmp(msg,"BAD IMAGE"));clean();
 fail_read=1;int inv=invalidate_count;
 assert(!c3_playback_show_saved(5,C3_OUTPUT_BMP)&&!strcmp(msg,"READ ERROR")&&invalidate_count==inv+1);clean();fail_read=0;
 fail_close=1;assert(!c3_playback_show_saved(5,C3_OUTPUT_DNG));clean();fail_close=0;
 card=0;assert(!c3_playback_show_saved(5,C3_OUTPUT_DNG));clean();card=1;
 assert(mounts==mount_count);fail_dir=0;
 puts("PASS direct committed-photo review: exact ID + suffix, no remount/scan, no browse controls, read and close faults");
}
#endif
static void test_nav(void){
 c3_ui_nav_t n;c3_settings_t s;c3_settings_defaults(&s);c3_settings_t before=s;
 c3_ui_nav_reset(&n,0,0);c3_button_events_t longfilm={0};longfilm.long_pressed=C3_BTN_FILM;
 assert(c3_ui_nav_poll(&n,press(C3_BTN_ISO),0,0,30,&s)==0&&n.page==C3_UI_PREVIEW);
 assert(c3_ui_nav_poll(&n,longfilm,C3_BTN_FILM,0,700,&s)==C3_NAV_MODE_OPEN);
 assert(c3_ui_nav_poll(&n,press(0),0,0,750,&s)==0&&n.page==C3_UI_MODE);
 assert(c3_ui_nav_poll(&n,press(C3_BTN_ISO),C3_BTN_ISO,0,800,&s)==C3_NAV_PLAY_OPEN&&n.page==C3_UI_PLAY);
 assert(c3_ui_nav_poll(&n,press(0),0,15,810,&s)==0);
 assert(c3_ui_nav_poll(&n,press(0),0,15,870,&s)==C3_NAV_PLAY_STEP&&n.ev_step_delta==-1);
 assert(c3_ui_nav_poll(&n,press(0),0,0,880,&s)==0);
 assert(c3_ui_nav_poll(&n,press(0),0,0,940,&s)==C3_NAV_PLAY_STEP&&n.ev_step_delta==1);
 assert(c3_ui_nav_poll(&n,press(C3_BTN_4X),0,0,960,&s)==0&&n.page==C3_UI_PLAY);
 assert(c3_ui_nav_poll(&n,press(C3_BTN_ISO),0,0,970,&s)==0&&n.page==C3_UI_PLAY);
 assert(c3_ui_nav_poll(&n,press(C3_BTN_SHUTTER|C3_BTN_ISO),C3_BTN_FILM,0,990,&s)==C3_NAV_BACK&&n.page==C3_UI_PREVIEW&&n.inhibit_film);
 assert(c3_ui_nav_poll(&n,longfilm,C3_BTN_FILM,0,1800,&s)==0);
 assert(!memcmp(&s,&before,sizeof s));
 puts("PASS navigation: ISO only in lever MODE opens PLAY, dial debounce/wrap, no EV/settings mutation, 4X ignored, SHOT exit priority + lever inhibition");
}
int main(void){test_play();test_nav();
#if C3_POSTSHOT
 test_immediate();
#endif
#if C3_I18N
 c3_i18n_set_language(1);c3_playback_t s={0};
 assert(c3_playback_enter(&s)&&s.key==15);clean();
 assert(c3_playback_step(&s,-1)&&s.key==11&&rows==240);clean();
 assert(c3_playback_toggle_info(&s)&&!s.info&&rows==240);clean();
 card=0;assert(!c3_playback_enter(&s));clean();card=1;
 puts("PASS Chinese playback: mixed photo navigation, info and no-card, same read-only SD / DMA invariants");
#endif
 return 0;}

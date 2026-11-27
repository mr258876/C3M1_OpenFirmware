/* Native fault injection that compiles the production c3_storage.c with a
 * deterministic fake FIFO and fake FatFs. No real SD or MCU is required. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "c3_storage_host_shim.h"
#include "c3_storage.h"
#include "c3_capture_recovery.h"
#include "c3_bmp.h"
#include "c3_dng.h"

#define MAX_FILES 16
#define MAX_DATA (C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES+C3_DNG_TAIL_BYTES)
static struct {
    char name[17];
    uint8_t *data;
    uint32_t length;
    int present;
} files[MAX_FILES];
static int media_present = 1, fail_write_after = -1, fail_sync = 0;
static int fail_close = 0, fail_rename = 0, fail_rewind = 0;
static unsigned rewind_count = 0, end_count = 0, read_count = 0;
FATFS SDFatFS;
char SDPath[] = "0:";

static int lookup(const char *path)
{
    for(int i=0;i<MAX_FILES;++i)
        if(files[i].present && strcmp(path,files[i].name)==0) return i;
    return -1;
}
static int free_slot(void)
{
    for(int i=0;i<MAX_FILES;++i) if(!files[i].present) return i;
    return -1;
}
static int contains(const char *path) { return lookup(path)>=0; }
FRESULT f_mount(FATFS *fs, const char *path, unsigned opt)
{
    (void)path; (void)opt;
    return (!fs || media_present) ? FR_OK : FR_DISK_ERR;
}
FRESULT f_opendir(DIR *d, const char *path)
{
    (void)path; d->iterator=0; return media_present ? FR_OK : FR_DISK_ERR;
}
FRESULT f_readdir(DIR *d, FILINFO *info)
{
    while(d->iterator<MAX_FILES) {
        unsigned i=d->iterator++;
        if(files[i].present) {
            strcpy(info->fname,files[i].name+3);
            return FR_OK;
        }
    }
    info->fname[0]=0;
    return FR_OK;
}
FRESULT f_closedir(DIR *d) { (void)d; return FR_OK; }
FRESULT f_stat(const char *path, FILINFO *info)
{
    (void)info;
    if(!media_present) return FR_DISK_ERR;
    return contains(path)?FR_OK:FR_NO_FILE;
}
FRESULT f_open(FIL *f,const char *path,unsigned flags)
{
    assert(flags==(FA_WRITE|FA_CREATE_NEW));
    if(!media_present) return FR_DISK_ERR;
    if(contains(path)) return FR_EXIST;
    int i=free_slot();
    if(i<0) return FR_DISK_ERR;
    files[i].present=1;
    strcpy(files[i].name,path);
    files[i].length=0;
    if(!files[i].data) files[i].data=calloc(MAX_DATA,1);
    assert(files[i].data);
    f->entry=(unsigned)i; f->offset=0;
    return FR_OK;
}
FRESULT f_write(FIL *f,const void *buf,UINT n,UINT *wrote)
{
    assert(f->entry<MAX_FILES);
    if(!media_present) { *wrote=0;return FR_DISK_ERR; }
    if(fail_write_after>=0 && (int)(f->offset+n)>fail_write_after) {
        fail_write_after=-1;
        *wrote=0;
        return FR_DISK_ERR;
    }
    assert(f->offset+n<=MAX_DATA);
    memcpy(files[f->entry].data+f->offset,buf,n);
    f->offset+=n;
    if(f->offset>files[f->entry].length) files[f->entry].length=f->offset;
    *wrote=n;
    return FR_OK;
}
FRESULT f_sync(FIL *f)
{
    (void)f;
    if(fail_sync) { fail_sync=0; return FR_DISK_ERR; }
    return FR_OK;
}
FRESULT f_close(FIL *f)
{
    (void)f;
    if(fail_close) {fail_close=0;return FR_DISK_ERR;}
    return FR_OK;
}
FRESULT f_unlink(const char *path)
{
    int i=lookup(path);
    if(i<0) return FR_NO_FILE;
    files[i].present=0;
    return FR_OK;
}
FRESULT f_rename(const char *oldpath,const char *newpath)
{
    if(fail_rename) { fail_rename=0; return FR_DISK_ERR; }
    int i=lookup(oldpath);
    if(i<0 || contains(newpath)) return FR_DISK_ERR;
    strcpy(files[i].name,newpath);
    return FR_OK;
}
uint8_t c3_camera_raw_rewind(void)
{
    if(fail_rewind) {fail_rewind=0;return 0;}
    rewind_count++;
    read_count=0;
    return 1;
}
uint8_t c3_camera_fifo_read_fast(void)
{
    uint32_t i=read_count++;
    assert(i<C3_RAW_SENSOR_BYTES);
    return (uint8_t)((i*17u+43u)&255u);
}
void c3_camera_raw_read_end(void) { end_count++; }
static void assert_dng(const char *path)
{
    int i=lookup(path);
    assert(i>=0);
    assert(files[i].length==MAX_DATA);
    uint8_t *p=files[i].data;
    assert(p[0]=='I' && p[1]=='I' && p[2]==42);
#if C3_DNG_BLACK_CALIBRATION
    uint8_t expected_header[C3_DNG_HEADER_BYTES];
    c3_dng_make_header(expected_header);
    assert(memcmp(p,expected_header,sizeof(expected_header))==0);
    /* Fixed independent fixture, 16x / 1/300, nonuniform channels. */
    const uint8_t *bl=p+C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES+136u;
    const uint32_t expected[]={12854u,13510u,14941u,15475u};
    for (unsigned c=0;c<(C3_DNG_ACR_MODE==1 ? 1u : 4u);++c) {
        uint32_t n=(uint32_t)bl[8*c]|((uint32_t)bl[8*c+1]<<8)|((uint32_t)bl[8*c+2]<<16)|((uint32_t)bl[8*c+3]<<24);
        uint32_t d=(uint32_t)bl[8*c+4]|((uint32_t)bl[8*c+5]<<8)|((uint32_t)bl[8*c+6]<<16)|((uint32_t)bl[8*c+7]<<24);
        uint32_t want=C3_DNG_ACR_MODE==1 ?
            (expected[0]+expected[1]+expected[2]+expected[3]+2u)/4u :
            expected[c]*C3_DNG_SCALE;
        assert(n==want && d==1000u);
    }
#endif
    for (uint32_t y=0; y<C3_RAW_HEIGHT; ++y)
        for (uint32_t x=0; x<C3_RAW_WIDTH; ++x) {
            /* Must retain the exact input sample [y][x], NOT [y*642+x]
             * from a continuous FIFO stream. This catches row misalignment. */
            uint32_t fifo_at=y*C3_RAW_SENSOR_WIDTH+x;
            uint32_t dng_at=y*C3_RAW_WIDTH+x;
#if C3_DNG_SAMPLE_BYTES == 2
            assert(p[C3_DNG_HEADER_BYTES+2u*dng_at]==0u);
#endif
            assert(p[C3_DNG_HEADER_BYTES+C3_DNG_SAMPLE_BYTES*dng_at+
                     C3_DNG_SAMPLE_BYTES-1u]==(uint8_t)((fifo_at*17u+43u)&255u));
        }
    assert(read_count==C3_RAW_SENSOR_BYTES);
#if C3_DNG_SOFT_PROFILE
    uint8_t expected_tail[C3_DNG_TAIL_BYTES];
    c3_dng_make_tail(expected_tail);
    assert(memcmp(p+C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES,
                  expected_tail,C3_DNG_TAIL_BYTES)==0);
#endif
}
static void assert_bmp(const char *path)
{
    int i=lookup(path);
    assert(i>=0);
    assert(files[i].length==C3_BMP_FILE_BYTES);
    const uint8_t *p=files[i].data;
    assert(p[0]=='B' && p[1]=='M');
    assert(p[18]==0x40 && p[19]==0x01); /* width=320 */
    assert(p[22]==0x10 && p[23]==0xFF && p[24]==0xFF && p[25]==0xFF); /* -240 */
    assert(p[28]==24 && p[29]==0);
    assert(p[30]==0 && p[31]==0);
    for (unsigned k=0;k<C3_BMP_WIDTH*C3_BMP_HEIGHT;++k) {
        uint16_t col=(uint16_t)(((k*2u*17u+43u)&255u)<<8u) |
                     (uint16_t)(((k*2u+1u)*17u+43u)&255u);
        uint8_t check[3];c3_bmp_pixel_bgr(col,check);
        for (unsigned b=0;b<3u;b++)
            assert(p[C3_BMP_HEADER_BYTES+k*3u+b]==check[b]);
    }
}
int main(int argc, char **argv)
{
    c3_capture_recovery_t s;
    c3_recovery_reset(&s);
    assert(!c3_recovery_can_retry(&s) && !c3_recovery_has_frame(&s));
    c3_recovery_captured(&s);
    assert(c3_recovery_has_frame(&s));

#if C3_DNG_BLACK_CALIBRATION
    c3_dng_begin_shot();
    c3_dng_set_black_level(5u,0xF0u,0u);
#endif
    /* No card: don't consume a single byte, preserve a pending frame. */
    media_present=0;
    assert(c3_storage_save_dng()==C3_SAVE_NO_SD);
    c3_recovery_save_finished(&s,C3_SAVE_NO_SD);
    assert(c3_recovery_can_retry(&s) && rewind_count==0);
    media_present=1;
    assert(c3_storage_save_dng()==C3_SAVE_OK);
    c3_recovery_save_finished(&s,C3_SAVE_OK);
    assert(!c3_recovery_has_frame(&s));
    assert_dng("0:/C3_00001.DNG");

    /* Mid-stream write error: partial TMP must be cleaned; same RAW retried. */
    c3_recovery_captured(&s);
    fail_write_after=10000;
    assert(c3_storage_save_dng()==C3_SAVE_WRITE_ERROR);
    c3_recovery_save_finished(&s,C3_SAVE_WRITE_ERROR);
    assert(c3_recovery_can_retry(&s));
    assert(!contains("0:/C3_00002.DNG"));
    assert(!contains("0:/C3_00002.TMP"));
    assert(c3_storage_save_dng()==C3_SAVE_OK);
    assert_dng("0:/C3_00002.DNG");
    c3_recovery_save_finished(&s,C3_SAVE_OK);
    assert(!c3_recovery_has_frame(&s));

    /* Successful write + rename error: preserve complete TMP, skip on retry. */
    c3_recovery_captured(&s);
    fail_rename=1;
    assert(c3_storage_save_dng()==C3_SAVE_RENAME_ERROR);
    assert(!contains("0:/C3_00003.DNG"));
    assert_dng("0:/C3_00003.TMP");
    c3_recovery_save_finished(&s,C3_SAVE_RENAME_ERROR);
    assert(c3_storage_save_dng()==C3_SAVE_OK);
    assert_dng("0:/C3_00004.DNG");
#if C3_DNG_BLACK_CALIBRATION
    int tmp=lookup("0:/C3_00003.TMP"),final=lookup("0:/C3_00004.DNG");
    assert(memcmp(files[tmp].data,files[final].data,MAX_DATA)==0);
#endif
    c3_recovery_save_finished(&s,C3_SAVE_OK);

    /* Sync failures must not publish final DNG, and retry must succeed. */
    c3_recovery_captured(&s);
    fail_sync=1;
    assert(c3_storage_save_dng()==C3_SAVE_SYNC_ERROR);
    assert(!contains("0:/C3_00005.DNG"));
    assert(!contains("0:/C3_00005.TMP"));
    c3_recovery_save_finished(&s,C3_SAVE_SYNC_ERROR);
    assert(c3_storage_save_dng()==C3_SAVE_OK);
    assert_dng("0:/C3_00005.DNG");

    /* Close failure: do not silently delete possibly recoverable TMP. */
    c3_recovery_captured(&s);
    fail_close=1;
    assert(c3_storage_save_dng()==C3_SAVE_CLOSE_ERROR);
    assert_dng("0:/C3_00006.TMP");
    c3_recovery_save_finished(&s,C3_SAVE_CLOSE_ERROR);
    assert(c3_storage_save_dng()==C3_SAVE_OK);
    assert_dng("0:/C3_00007.DNG");

    /* Invalid FIFO hold: refuse to generate a fake DNG. */
    c3_recovery_captured(&s);
    fail_rewind=1;
    assert(c3_storage_save_dng()==C3_SAVE_FIFO_ERROR);
    c3_recovery_save_finished(&s,C3_SAVE_FIFO_ERROR);
    assert(c3_recovery_can_retry(&s));
    assert(!contains("0:/C3_00008.TMP"));
    c3_recovery_discard(&s);
    assert(!c3_recovery_has_frame(&s));

    /* BMP output: the same protected FIFO is read as RGB565, not DNG.
     * Simulate an interrupted write, then re-read the exact same pixels. */
    media_present=0;
    assert(c3_storage_save(C3_OUTPUT_BMP)==C3_SAVE_NO_SD);
    media_present=1;
    fail_write_after=10000;
    assert(c3_storage_save(C3_OUTPUT_BMP)==C3_SAVE_WRITE_ERROR);
    assert(!contains("0:/C3_00008.BMP") && !contains("0:/C3_00008.TMP"));
    assert(c3_storage_save(C3_OUTPUT_BMP)==C3_SAVE_OK);
    assert_bmp("0:/C3_00008.BMP");
    if (argc>1) {
        int bmp_idx=lookup("0:/C3_00008.BMP");
        FILE *out=fopen(argv[1],"wb");
        assert(out && bmp_idx>=0);
        assert(fwrite(files[bmp_idx].data,1,files[bmp_idx].length,out)==files[bmp_idx].length);
        assert(fclose(out)==0);
    }
    assert(c3_storage_next_number()==9u);
    assert(c3_storage_mount());
    assert(c3_storage_next_number()==9u); /* both DNG and BMP counted */
    assert(c3_storage_save(C3_OUTPUT_DNG)==C3_SAVE_OK);
    assert_dng("0:/C3_00009.DNG");
#if C3_DNG_SOFT_PROFILE
    /* NEW: a failure AFTER all RAW bytes but DURING profile-tail append.
     * DNG must not publish a file whose IFD references missing tail bytes. */
    fail_write_after=(int)(C3_DNG_HEADER_BYTES+C3_DNG_STRIP_BYTES);
    assert(c3_storage_save_dng()==C3_SAVE_WRITE_ERROR);
    assert(!contains("0:/C3_00010.DNG"));
    assert(!contains("0:/C3_00010.TMP"));
    assert(c3_storage_save_dng()==C3_SAVE_OK);
    assert_dng("0:/C3_00010.DNG");
    assert(rewind_count==14u);
    assert(end_count==14u);
#else
    assert(rewind_count==12u);
    assert(end_count==12u); /* each successful rewind eventually calls end */
#endif

    if (argc>2) {
        int i=lookup("0:/C3_00001.DNG");
        FILE *out=fopen(argv[2],"wb"); assert(out && i>=0);
        assert(fwrite(files[i].data,1,files[i].length,out)==files[i].length);
        assert(fclose(out)==0);
    }
    printf("PASS: SD absence, byte-exact RAW retry, partial write cleanup, "
           "rename/sync/close faults, FIFO guard, explicit discard\\n");
    for(unsigned i=0;i<MAX_FILES;i++) free(files[i].data);
    return 0;
}

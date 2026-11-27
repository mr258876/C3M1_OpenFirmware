#include "c3_storage.h"
#include "c3_dng.h"
#include "c3_bmp.h"
#include "c3_camera.h"
#include "fatfs.h"

static uint8_t mounted;
static uint32_t next_photo=1;
/* FatFs / SDIO may access buffers as words: ensure 4-byte alignment. */
static uint8_t block[4096] __attribute__((aligned(4)));
static uint8_t header[C3_DNG_HEADER_BYTES] __attribute__((aligned(4)));

static int parse_number(const char *name)
{
    /* Strict 8.3 file name C3_12345.DNG; case-insensitive suffix. */
    if(name[0]!='C' || name[1]!='3' || name[2]!='_') return 0;
    uint32_t num=0;
    for(int i=3;i<8;i++) {
        if(name[i]<'0' || name[i]>'9') return 0;
        num=num*10u+(uint32_t)(name[i]-'0');
    }
    if(name[8]!='.' || name[12]!=0) return 0;
    /* The numeric photo sequence is shared across DNG and BMP. */
    uint8_t dng=(uint8_t)((name[9]=='D'||name[9]=='d') &&
                          (name[10]=='N'||name[10]=='n') &&
                          (name[11]=='G'||name[11]=='g'));
    uint8_t bmp=(uint8_t)((name[9]=='B'||name[9]=='b') &&
                          (name[10]=='M'||name[10]=='m') &&
                          (name[11]=='P'||name[11]=='p'));
    if (!dng && !bmp) return 0;
    return (int)num;
}
uint8_t c3_storage_mount(void)
{
    DIR dir;
    FILINFO info;
    mounted=0;
    /* Re-register the volume for retries, including a physically swapped SD.
     * An open transaction must be closed before calling this function. */
    (void)f_mount(0, SDPath, 0);
    if(f_mount(&SDFatFS,SDPath,1)!=FR_OK) return 0;
    if(f_opendir(&dir,"0:/")!=FR_OK) return 0;
    uint32_t max_no=0;
    FRESULT scan_status=FR_OK;
    for(;;) {
        scan_status=f_readdir(&dir,&info);
        if(scan_status!=FR_OK || info.fname[0]==0) break;
        int n=parse_number(info.fname);
        if(n>0 && (uint32_t)n>max_no) max_no=(uint32_t)n;
    }
    FRESULT close_status=f_closedir(&dir);
    if(scan_status!=FR_OK || close_status!=FR_OK) return 0;
    next_photo=max_no+1u;
    if(next_photo==0u) next_photo=100000u;
    mounted=1;
    return 1;
}
/* Foreground only, after explicit confirmation and with all file I/O stopped.
 * One SD drive at 0:. Reinitialize even if FatFs cached initialization. */
extern Disk_drvTypeDef disk;
c3_format_result_t c3_storage_format_card(void)
{
    DWORD sectors=0;
    WORD sector_size=0;
    mounted=0u;
    if (SDPath[0]!='0' || SDPath[1]!=':' || !disk.drv[0]) return C3_FORMAT_NOT_READY;
    if (f_mount(0,SDPath,0)!=FR_OK) return C3_FORMAT_NOT_READY;
    disk.is_initialized[0]=0;
    DSTATUS stat=disk_initialize(0);
    if ((stat&(STA_NOINIT|STA_NODISK|STA_PROTECT)) || disk_status(0)!=0 ||
        disk_ioctl(0,GET_SECTOR_COUNT,&sectors)!=RES_OK ||
        disk_ioctl(0,GET_SECTOR_SIZE,&sector_size)!=RES_OK ||
        sector_size!=512 || sectors<128u) {
        disk.is_initialized[0]=0;
        return C3_FORMAT_NOT_READY;
    }
    if(sectors>67108864u) return C3_FORMAT_TOO_LARGE;
    if(f_mount(&SDFatFS,SDPath,0)!=FR_OK) return C3_FORMAT_NOT_READY;
    /* Legacy FatFs API: 0=single MBR partition, auto cluster size. */
    if(f_mkfs(SDPath,0,0)!=FR_OK) {
        (void)f_mount(0,SDPath,0); disk.is_initialized[0]=0;
        return C3_FORMAT_FAILED;
    }
    if(!c3_storage_mount() || next_photo!=1u) {
        mounted=0u; (void)f_mount(0,SDPath,0); disk.is_initialized[0]=0;
        return C3_FORMAT_MOUNT_ERROR;
    }
    return C3_FORMAT_OK;
}
void c3_storage_invalidate(void) { mounted=0u; }
uint32_t c3_storage_next_number(void) { return next_photo; }
static void filename(char str[17],uint32_t num,c3_output_format_t format)
{
    static const char tpl[]="0:/C3_00000.DNG";
    for(unsigned i=0;i<sizeof(tpl);i++) str[i]=tpl[i];
    for(int i=10;i>=6;i--) { str[i]=(char)('0'+(num%10u)); num/=10u; }
    if (format == C3_OUTPUT_BMP) {
        str[12]='B'; str[13]='M'; str[14]='P';
    }
}
static uint8_t write_all(FIL *f,const uint8_t *p,UINT n)
{
    UINT wrote=0;
    return (f_write(f,p,n,&wrote)==FR_OK && wrote==n);
}
/* A DNG/BMP is exposed under its final filename ONLY after the entire
 * image is written, synchronized and closed. FAT rename is not power-fail
 * atomic, so a valid .TMP may remain after interrupted commit. */
c3_save_result_t c3_storage_save(c3_output_format_t format)
{
    if (format != C3_OUTPUT_DNG && format != C3_OUTPUT_BMP)
        return C3_SAVE_OPEN_ERROR;
    if (!mounted && !c3_storage_mount()) return C3_SAVE_NO_SD;
    if (next_photo > 99999u) return C3_SAVE_NO_SPACE;

    FIL file;
    FILINFO info;
    FRESULT r;
    char path[17], temp[17];
    uint32_t id = next_photo;

    /* Reserve both names. Do not overwrite finished images, nor a stale
     * .TMP that might contain data from an interrupted earlier save. */
    for (; id <= 99999u; ++id) {
        char alternate[17];
        filename(path, id, format);
        filename(alternate,id,format == C3_OUTPUT_DNG ? C3_OUTPUT_BMP : C3_OUTPUT_DNG);
        /* Never reuse a number already occupied by the OTHER format. */
        r = f_stat(path, &info);
        if (r == FR_OK) continue;
        if (r != FR_NO_FILE) {
            mounted = 0u;
            return C3_SAVE_OPEN_ERROR;
        }
        r = f_stat(alternate, &info);
        if (r == FR_OK) continue;
        if (r != FR_NO_FILE) {
            mounted = 0u;
            return C3_SAVE_OPEN_ERROR;
        }
        for (unsigned i = 0; i < sizeof(path); ++i) temp[i] = path[i];
        temp[12] = 'T'; temp[13] = 'M'; temp[14] = 'P';
        r = f_open(&file, temp, FA_WRITE | FA_CREATE_NEW);
        if (r == FR_EXIST) continue;
        if (r != FR_OK) {
            mounted = 0u;
            return C3_SAVE_OPEN_ERROR;
        }
        break;
    }
    if (id > 99999u) return C3_SAVE_NO_SPACE;

    c3_save_result_t status = C3_SAVE_OK;
    if (format == C3_OUTPUT_DNG) {
        c3_dng_make_header(header);
        if (!write_all(&file, header, C3_DNG_HEADER_BYTES)) {
            status = C3_SAVE_WRITE_ERROR;
            goto finish;
        }
    } else {
        c3_bmp_make_header(header); /* preallocated 4-byte-aligned buffer */
        if (!write_all(&file, header, C3_BMP_HEADER_BYTES)) {
            status = C3_SAVE_WRITE_ERROR;
            goto finish;
        }
    }

    /* Retained FIFO is immutable until success or explicit user discard.
     * Rewind on EVERY retry; never recapture or change sensor format here. */
    if (!c3_camera_raw_rewind()) {
        status = C3_SAVE_FIFO_ERROR;
        goto finish;
    }
    if (format == C3_OUTPUT_DNG) {
        /* Keep the physical OV7725/FIFO geometry at 644 bytes per row.
         * Pixels [0,641] are valid; discard [642,643] after EVERY row.
         * Pack the 642-byte output rows into the existing 4096-byte block
         * so FatFs writes remain batched (not 480 tiny row writes).
         * If a write fails, leave the FIFO held for an exact rewind/retry. */
        UINT used = 0u;
        _Static_assert(sizeof(block)%C3_DNG_SAMPLE_BYTES==0u,
                       "DNG staging buffer must end on sample boundary");
        for (uint32_t y = 0u; y < C3_RAW_HEIGHT; ++y) {
            for (uint32_t x = 0u; x < C3_RAW_WIDTH; ++x) {
                block[used++] = c3_camera_fifo_read_fast();
                if (used == (UINT)sizeof(block)) {
                    if (!write_all(&file, block, used)) {
                        status = C3_SAVE_WRITE_ERROR;
                        break;
                    }
                    used = 0u;
                }
            }
            if (status != C3_SAVE_OK) break;
            for (uint32_t skip = C3_RAW_WIDTH;
                 skip < C3_RAW_SENSOR_WIDTH; ++skip)
                (void)c3_camera_fifo_read_fast();
        }
        if (status == C3_SAVE_OK && used != 0u &&
            !write_all(&file, block, used))
            status = C3_SAVE_WRITE_ERROR;
    } else {
        /* 320x240 RGB565 from the sensor ISP, 24-bit top-down BI_RGB BMP.
         * Buffer exactly four scanlines (3840B), zero full-frame buffering.
         * No OSD/peaking/grid is burned into the saved picture. */
        _Static_assert(C3_BMP_ROW_BYTES * 4u <= sizeof(block),
                       "BMP staging buffer too small");
        for (uint32_t y=0;y<C3_BMP_HEIGHT;y++) {
            uint8_t *row = block + (y%4u) * C3_BMP_ROW_BYTES;
            for (uint32_t x=0;x<C3_BMP_WIDTH;x++) {
                uint16_t pixel = (uint16_t)c3_camera_fifo_read_fast() << 8u;
                pixel |= c3_camera_fifo_read_fast();
                c3_bmp_pixel_bgr(pixel,row+x*3u);
            }
            if ((y%4u)==3u && !write_all(&file,block,4u*C3_BMP_ROW_BYTES)) {
                status = C3_SAVE_WRITE_ERROR;
                break;
            }
        }
    }
    c3_camera_raw_read_end();
    /* Append DNG profile AFTER the exact Bayer strip. The IFD offsets in
     * the prefix point here; a failed tail write must never publish a DNG.
     * This reuses the existing DMA-friendly 4096-byte aligned write buffer. */
    if (status == C3_SAVE_OK && format == C3_OUTPUT_DNG) {
        c3_dng_make_tail(block);
        if (!write_all(&file, block, C3_DNG_TAIL_BYTES))
            status = C3_SAVE_WRITE_ERROR;
    }
    if (status == C3_SAVE_OK && f_sync(&file) != FR_OK)
        status = C3_SAVE_SYNC_ERROR;

finish:
    /* Do not propagate a successful status after a failed file close. */
    r = f_close(&file);
    if (r != FR_OK && status == C3_SAVE_OK)
        status = C3_SAVE_CLOSE_ERROR;

    if (status == C3_SAVE_OK) {
        if (f_rename(temp, path) != FR_OK) {
            /* This .TMP contains a fully synced DNG; keep it for recovery. */
            mounted = 0u;
            return C3_SAVE_RENAME_ERROR;
        }
        next_photo = id + 1u;
        return C3_SAVE_OK;
    }

    /* Partial temporary files do not masquerade as complete photos.
     * Remove only when close succeeded; otherwise leave for inspection. */
    if (r == FR_OK) (void)f_unlink(temp);
    mounted = 0u;
    return status;
}


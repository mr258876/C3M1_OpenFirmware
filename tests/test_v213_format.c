#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "c3_storage.h"
#include "fatfs.h"
FATFS SDFatFS; char SDPath[4]="0:"; Disk_drvTypeDef disk;
static Diskio_drvTypeDef dummy;
static uint8_t *mem;
static DWORD sectors,fail_sector;
static unsigned writes,initializes;
static int init_fail,protect,read_fail,sync_fail,info_fail;
static WORD secsize;
DSTATUS disk_initialize(BYTE drv){assert(!drv);if(!disk.is_initialized[0]){initializes++;disk.is_initialized[0]=1;}return init_fail?STA_NOINIT:protect?STA_PROTECT:0;}
DSTATUS disk_status(BYTE drv){assert(!drv);return init_fail?STA_NOINIT:protect?STA_PROTECT:0;}
DRESULT disk_ioctl(BYTE drv,BYTE cmd,void *buf){assert(!drv);switch(cmd){case CTRL_SYNC:return sync_fail?RES_ERROR:RES_OK;case GET_SECTOR_COUNT:if(info_fail)return RES_ERROR;*(DWORD*)buf=sectors;return RES_OK;case GET_SECTOR_SIZE:*(WORD*)buf=secsize;return RES_OK;case GET_BLOCK_SIZE:*(DWORD*)buf=1;return RES_OK;}return RES_PARERR;}
DRESULT disk_read(BYTE drv,BYTE *buf,DWORD lba,UINT n){assert(!drv&&n&&lba<sectors&&n<=sectors-lba);if(read_fail)return RES_ERROR;memcpy(buf,mem+((uint64_t)lba*512),n*512u);return RES_OK;}
DRESULT disk_write(BYTE drv,const BYTE *buf,DWORD lba,UINT n){assert(!drv&&n&&lba<sectors&&n<=sectors-lba);if(protect||init_fail)return RES_ERROR;writes++;if(fail_sector>=lba&&fail_sector-lba<n)return RES_ERROR;memcpy(mem+((uint64_t)lba*512),buf,n*512u);return RES_OK;}
static void make(DWORD n){(void)f_mount(NULL,SDPath,0);if(mem)assert(!munmap(mem,(uint64_t)sectors*512));sectors=n;mem=mmap(NULL,(uint64_t)n*512,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);assert(mem!=MAP_FAILED);writes=initializes=0;disk.is_initialized[0]=1;disk.drv[0]=&dummy;init_fail=protect=read_fail=sync_fail=info_fail=0;secsize=512;fail_sector=UINT32_MAX;}
static void file_test(void){FIL f;UINT n;char b[8];assert(f_open(&f,"0:/C3_00042.BMP",FA_WRITE|FA_CREATE_NEW)==FR_OK);assert(f_write(&f,"1234567",7,&n)==FR_OK&&n==7);assert(f_close(&f)==FR_OK);assert(c3_storage_mount()&&c3_storage_next_number()==43);assert(f_open(&f,"0:/C3_00042.BMP",FA_READ)==FR_OK);assert(f_read(&f,b,7,&n)==FR_OK&&n==7&&!memcmp(b,"1234567",7));assert(f_close(&f)==FR_OK);}
int main(void){
 static const DWORD sizes[]={131072, 1048576, 4194304, 8388608, 16777216, 33554432, 67108864};
 for(unsigned i=0;i<sizeof sizes/sizeof *sizes;i++){
  make(sizes[i]);assert(c3_storage_format_card()==C3_FORMAT_OK);assert(initializes==1&&writes&&c3_storage_next_number()==1);
  unsigned typ=SDFatFS.fs_type;assert(typ==FS_FAT16||typ==FS_FAT32);file_test();assert(c3_storage_format_card()==C3_FORMAT_OK&&c3_storage_next_number()==1);
  FILINFO info;assert(f_stat("0:/C3_00042.BMP",&info)==FR_NO_FILE);printf("PASS %lu MiB FAT%u format/reformat/remount/read-write/number reset\n",(unsigned long)(sizes[i]/2048),typ==FS_FAT16?16:32);
 }
 make(67108865);assert(c3_storage_format_card()==C3_FORMAT_TOO_LARGE&&!writes);
 make(4194304);init_fail=1;assert(c3_storage_format_card()==C3_FORMAT_NOT_READY&&!writes);
 make(4194304);protect=1;assert(c3_storage_format_card()==C3_FORMAT_NOT_READY&&!writes);
 make(4194304);info_fail=1;assert(c3_storage_format_card()==C3_FORMAT_NOT_READY&&!writes);
 make(4194304);secsize=1024;assert(c3_storage_format_card()==C3_FORMAT_NOT_READY&&!writes);
 // On 8 GiB the new BPB is FAT32; inject every specific critical sector failure.
 DWORD fails[]={0,63,69,64,70};
 for(unsigned i=0;i<sizeof fails/sizeof *fails;i++){make(16777216);fail_sector=fails[i];assert(c3_storage_format_card()==C3_FORMAT_FAILED&&writes);}
 make(16777216);sync_fail=1;assert(c3_storage_format_card()==C3_FORMAT_FAILED);
 make(16777216);read_fail=1;assert(c3_storage_format_card()==C3_FORMAT_MOUNT_ERROR);
 assert(!munmap(mem,(uint64_t)sectors*512));puts("PASS no-card/write-protect/capacity/sector info guards (no writes), MBR/VBR/backup/FSInfo/sync faults, mount failure");return 0;
}

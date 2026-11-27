#ifndef C3_STORAGE_HOST_SHIM_H
#define C3_STORAGE_HOST_SHIM_H
#include <stdint.h>
#include "c3_dng.h"

typedef unsigned int UINT;
typedef enum {
    FR_OK = 0, FR_DISK_ERR = 1, FR_NOT_READY = 3, FR_NO_FILE = 4,
    FR_NO_PATH = 5, FR_DENIED = 7, FR_EXIST = 8
} FRESULT;
#define FA_WRITE 2u
#define FA_CREATE_NEW 4u

typedef struct { unsigned entry; unsigned offset; } FIL;
typedef struct { unsigned iterator; } DIR;
typedef struct { char fname[32]; } FILINFO;
typedef struct { int ignored; } FATFS;
extern FATFS SDFatFS;
extern char SDPath[];
FRESULT f_mount(FATFS *fs,const char *path,unsigned opt);
FRESULT f_opendir(DIR *d,const char *path);
FRESULT f_readdir(DIR *d,FILINFO *info);
FRESULT f_closedir(DIR *d);
FRESULT f_stat(const char *path,FILINFO *info);
FRESULT f_open(FIL *f,const char *path,unsigned flags);
FRESULT f_write(FIL *f,const void *buf,UINT n,UINT *written);
FRESULT f_sync(FIL *f);
FRESULT f_close(FIL *f);
FRESULT f_unlink(const char *path);
FRESULT f_rename(const char *oldpath,const char *newpath);
uint8_t c3_camera_raw_rewind(void);
uint8_t c3_camera_fifo_read_fast(void);
void c3_camera_raw_read_end(void);
#endif

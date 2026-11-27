#ifndef PLAYBACK_MOCK_FATFS_H
#define PLAYBACK_MOCK_FATFS_H
#include <stdint.h>
typedef unsigned UINT;
typedef enum {FR_OK,FR_DISK_ERR,FR_NO_FILE} FRESULT;
#define FA_READ 1u
#define AM_DIR 0x10u
#define AM_VOL 0x08u
typedef struct {uint32_t fsize,fptr;unsigned index;} FIL;
typedef struct {unsigned index;} DIR;
typedef struct {char fname[13];uint8_t fattrib;} FILINFO;
#define f_size(f) ((f)->fsize)
#define f_tell(f) ((f)->fptr)
FRESULT f_open(FIL*,const char*,unsigned);
FRESULT f_read(FIL*,void*,UINT,UINT*);
FRESULT f_lseek(FIL*,uint32_t);
FRESULT f_close(FIL*);
FRESULT f_opendir(DIR*,const char*);
FRESULT f_readdir(DIR*,FILINFO*);
FRESULT f_closedir(DIR*);
#endif

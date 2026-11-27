#ifndef C3_V161_SD_MOCK_FF_GEN_DRV_H
#define C3_V161_SD_MOCK_FF_GEN_DRV_H
#include <stdint.h>
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef unsigned int UINT;
typedef uint8_t DSTATUS;
typedef enum { RES_OK=0, RES_ERROR, RES_WRPRT, RES_NOTRDY, RES_PARERR } DRESULT;
#define _USE_WRITE 1
#define _USE_IOCTL 0
#define STA_NOINIT 1u
#define MSD_OK 0u
#define MSD_ERROR 1u
#define SD_DATATIMEOUT 100u
typedef struct {
    DSTATUS (*disk_initialize)(BYTE);
    DSTATUS (*disk_status)(BYTE);
    DRESULT (*disk_read)(BYTE,BYTE *,DWORD,UINT);
    DRESULT (*disk_write)(BYTE,const BYTE *,DWORD,UINT);
} Diskio_drvTypeDef;
uint8_t BSP_SD_Init(void);
uint8_t BSP_SD_GetCardState(void);
uint8_t BSP_SD_ReadBlocks(uint32_t *,uint32_t,uint32_t,uint32_t);
uint8_t BSP_SD_WriteBlocks(uint32_t *,uint32_t,uint32_t,uint32_t);
#endif

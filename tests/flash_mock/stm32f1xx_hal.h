#ifndef TEST_FLASH_MOCK_HAL_H
#define TEST_FLASH_MOCK_HAL_H
#include <stdint.h>
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;
typedef struct {
    uint32_t TypeErase;
    uint32_t PageAddress;
    uint32_t NbPages;
} FLASH_EraseInitTypeDef;
#define FLASH_TYPEERASE_PAGES 0x1u
#define FLASH_TYPEPROGRAM_HALFWORD 0x2u
HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *e, uint32_t *error_page);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,uint32_t addr,uint64_t data);
#endif

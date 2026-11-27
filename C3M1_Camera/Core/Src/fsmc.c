/* C3M1 ST7789 parallel LCD via 8-bit FSMC NE4 / A10. */
#include "fsmc.h"

/* v0.1.38: field-tested LCD write timing DATAST=4 (ADDSET unchanged).
 * Override with C3_FSMC_DATAST=8,6,5,4 for conservative comparisons.
 * This does not alter the 72MHz HCLK or the FIFO RCLK timing.
 */

SRAM_HandleTypeDef hsram1;
void MX_FSMC_Init(void)
{
    FSMC_NORSRAM_TimingTypeDef t = {0};
    hsram1.Instance = FSMC_NORSRAM_DEVICE;
    hsram1.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    hsram1.Init.NSBank = FSMC_NORSRAM_BANK4;
    hsram1.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    hsram1.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    hsram1.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    hsram1.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    hsram1.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    hsram1.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    hsram1.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    hsram1.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    hsram1.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    hsram1.Init.ExtendedMode = FSMC_EXTENDED_MODE_DISABLE;
    hsram1.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    hsram1.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    /* ADDSET is fixed; only DATAST is varied in the speed sweep. */
    t.AddressSetupTime = 2;
    t.AddressHoldTime = 1;
    t.DataSetupTime = C3_FSMC_DATAST;
    t.BusTurnAroundDuration = 2;
    t.CLKDivision = 2;
    t.DataLatency = 2;
    t.AccessMode = FSMC_ACCESS_MODE_A;
    if (HAL_SRAM_Init(&hsram1, &t, &t) != HAL_OK) Error_Handler();
    __HAL_AFIO_FSMCNADV_DISCONNECTED();
}

static uint8_t initialized;
void HAL_SRAM_MspInit(SRAM_HandleTypeDef *h)
{
    (void)h;
    if (initialized) return;
    initialized = 1;
    __HAL_RCC_FSMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Mode = GPIO_MODE_AF_PP; g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    /* NE4 (unused by grounded-CS LCD), A10 command/data. */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOG, &g);
    /* D0-D3 PD14/PD15/PD0/PD1; RD=PD4, WR=PD5. */
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
            GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &g);
    /* D4-D7 PE7/PE8/PE9/PE10. */
    g.Pin = GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOE, &g);
}
void HAL_SRAM_MspDeInit(SRAM_HandleTypeDef *h)
{
    (void)h;
    HAL_GPIO_DeInit(GPIOG, GPIO_PIN_0 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5 |
                          GPIO_PIN_14 | GPIO_PIN_15);
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10);
    __HAL_RCC_FSMC_CLK_DISABLE();
    initialized = 0;
}

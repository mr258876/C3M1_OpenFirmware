/**
 ****************************************************************************************************
 * @file        ov7725.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2020-04-26
 * @brief       OV7725 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 STM32F103开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 * 修改说明
 * V1.0 20200426
 * 第一次发布
 *
 ****************************************************************************************************
 */

#ifndef _OV7725_H
#define _OV7725_H

#include "../../SYSTEM/sys/sys.h"


/******************************************************************************************/
/* OV7725模块的的 VSYNC/WRST/RRST/OE/RCLK/WEN 引脚 定义
 * D0~D7, 由于引脚多, 且连续, 这里就不在这里定义了,直接在ov7725_init里面修改.所以在移植
 * 的时候, 除了改这6个IO口, 还得改ov7725_init里面的D0~D7所在的IO口.
 */
/******************************************************************************************/

/* OV7725相关端口定义 */
#define OV7725_VSYNC(x)   do{ x ? \
                              HAL_GPIO_WritePin(OV7725_VSYNC_GPIO_Port, OV7725_VSYNC_Pin, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(OV7725_VSYNC_GPIO_Port, OV7725_VSYNC_Pin, GPIO_PIN_RESET); \
                          }while(0)  		/* VSYNC */

#define OV7725_WRST(x)    do{ x ? \
                              HAL_GPIO_WritePin(OV7725_WRST_GPIO_Port, OV7725_WRST_Pin, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(OV7725_WRST_GPIO_Port, OV7725_WRST_Pin, GPIO_PIN_RESET); \
                          }while(0)    		/* WRST */

#define OV7725_RRST(x)    do{ x ? \
                              HAL_GPIO_WritePin(OV7725_RRST_GPIO_Port, OV7725_RRST_Pin, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(OV7725_RRST_GPIO_Port, OV7725_RRST_Pin, GPIO_PIN_RESET); \
                          }while(0)    		/* RRST */
                          
#define OV7725_OE(x)      do{ x ? \
                              HAL_GPIO_WritePin(OV7725_OE_GPIO_Port, OV7725_OE_Pin, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(OV7725_OE_GPIO_Port, OV7725_OE_Pin, GPIO_PIN_RESET); \
                          }while(0)        	/* OE */

#define OV7725_RCLK(x)      x ? (OV7725_RCLK_GPIO_Port->BSRR = OV7725_RCLK_Pin) : (OV7725_RCLK_GPIO_Port->BRR = OV7725_RCLK_Pin)    /* RCLK */
//#define OV7725_RCLK(x)      HAL_GPIO_TogglePin(OV7725_RCLK_GPIO_PORT,OV7725_RCLK_GPIO_PIN);

#define OV7725_WEN(x)     do{ x ? \
                              HAL_GPIO_WritePin(OV7725_WEN_GPIO_Port, OV7725_WEN_Pin, GPIO_PIN_SET) : \
                              HAL_GPIO_WritePin(OV7725_WEN_GPIO_Port, OV7725_WEN_Pin, GPIO_PIN_RESET); \
                          }while(0)      	/* WEN */

#define OV7725_DATA         ((GPIOG->IDR >> 1) & 0X00FFu)                                                 /* D0~D7 */

/*这边是我新加的*/

#define FIFO_RE_LOW()  (RE_GPIO_Port->BSRR = RE_Pin << 16)  // 拉低（x=0）
#define FIFO_RE_HIGH() (RE_GPIO_Port->BSRR = RE_Pin)        // 拉高（x=1）


/* OV7725 ID */
#define OV7725_MID              0X7FA2      /* MID , 第一个MID*/
#define OV7725_MID1             0X7FFF      /* MID1, 第二个MID */
#define OV7725_PID              0X7721

/* OV7670的SCCB地址 */
#define OV7725_ADDR             0X42


//void ov7725_init(void);                             /* OV7725初始化 */
extern volatile uint32_t c3_ov_last_mid;
extern volatile uint32_t c3_ov_last_pid;
extern volatile uint32_t c3_ov_last_bad_reg;
extern volatile uint32_t c3_ov_last_bad_phase;
extern volatile uint32_t c3_ov_last_verify_expected;
extern volatile uint32_t c3_ov_last_verify_actual;
extern volatile uint32_t c3_ov_last_dc_reg;
extern volatile uint32_t c3_ov_last_dc_bits;
/* Preview-only test: a post-configuration readback mismatch is recorded,
 * but doesn't veto preview when PID and frame sync are both valid. */
#define C3_OV_WARN_MTX1  (1u << 0)
#define C3_OV_WARN_MTX2  (1u << 1)
#define C3_OV_WARN_DSP4  (1u << 2)
#define C3_OV_WARN_PID_POST (1u << 3)
extern volatile uint32_t c3_ov_verify_warn_mask;
extern volatile uint32_t c3_ov_warn_first_reg;
extern volatile uint32_t c3_ov_warn_first_expected;
extern volatile uint32_t c3_ov_warn_first_actual;
uint8_t ov7725_init(void);

uint8_t ov7725_RAW_init(void);
uint8_t ov7725_verify_raw_blc_capture(void);
extern volatile uint32_t c3_ov_blc_rgb_readback;
extern volatile uint32_t c3_ov_blc_raw_init_readback;
extern volatile uint32_t c3_ov_blc_raw_capture_readback;
extern volatile uint32_t c3_ov_blc_raw_gain_readback;
#if C3_FAST_RAW_SWITCH
uint8_t ov7725_RAW_fast_init(void);
#if C3_FAST_RGB_RESTORE
/* Return RAW8 -> RGB565 without COM7 software reset. Leaves AEC/AGC/AWB off
 * for the caller to restore before re-enabling preview. */
uint8_t ov7725_RGB_fast_restore(void);
#endif
#endif                      /* OV7725RAW初始化测试 */

uint8_t ov7725_read_reg(uint16_t reg);
uint8_t ov7725_read_reg_restart(uint8_t reg);
extern volatile uint32_t c3_sccb_restart_attempts;
extern volatile uint32_t c3_sccb_restart_successes;
extern volatile uint32_t c3_sccb_restart_last_reg;
/* Bus read primitive; 1=transaction performed, NOT guaranteed valid sensor data. */
uint8_t ov7725_read_reg_checked(uint8_t reg, uint8_t *value);              /* OV7725 读寄存器 */
uint8_t ov7725_write_reg(uint8_t reg, uint8_t data);/* OV7725写寄存器 */

void ov7725_light_mode(uint8_t mode);       /* OV7725 灯光模式设置 */
void ov7725_color_saturation(uint8_t sat);  /* OV7725 色彩饱和度设置 */
void ov7725_brightness(uint8_t bright);     /* OV7725 亮度设置 */
void ov7725_contrast(uint8_t contrast);     /* OV7725 对比度设置 */
void ov7725_special_effects(uint8_t eft);   /* OV7725 特效设置 */

uint8_t ov7725_preview_window_set(uint8_t zoom4x); /* VGA centre crop/QVGA, 320x240 RGB565 */
uint8_t ov7725_window_set(uint16_t width, uint16_t height);  /* OV7725 输出窗口设置 */

#endif






















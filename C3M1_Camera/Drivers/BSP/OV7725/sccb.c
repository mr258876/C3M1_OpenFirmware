/**
 ****************************************************************************************************
 * @file        sccb.c
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2020-04-26
 * @brief       SCCB 驱动代码
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

#include "sccb.h"
#include "../../SYSTEM/delay/delay.h"

/**
 * @brief       初始化SCCB
 * @param       无
 * @retval      无
 */
void sccb_init(void)
{
	sccb_stop();     /* 停止总线上所有设备 */
}

/**
 * @brief       SCCB延时函数,用于控制IIC读写速度
 * @param       无
 * @retval      无
 */
/* v0.1.50: real hardware showed that PG13 left in released open-drain
 * output mode misreads SCCB data bytes with bit7=1 (e.g. A2, 93, B0).
 * Switching only the eight incoming data bits to INPUT FLOATING produced
 * correct readings at LOW/EARLY/LATE sampling points on the same board.
 * SCCB still drives START/address/subaddress/NACK/STOP as open-drain.
 * C3_SCCB_FLOAT_READ=0 restores the old receiver for A/B rollback.
 * This does NOT prove that the output-mode anomaly is understood electrically.
 */
#ifndef C3_SCCB_FLOAT_READ
#define C3_SCCB_FLOAT_READ 1
#endif
#if (C3_SCCB_FLOAT_READ != 0) && (C3_SCCB_FLOAT_READ != 1)
#error "C3_SCCB_FLOAT_READ must be 0 or 1"
#endif

/* v0.1.46: the hardware showed intermittent FF reads at 20us.
 * Keep a configurable half-period; 50us is the conservative boot default.
 * The scan diagnostic can try 50, 100 and 200us without touching HCLK.
 * This is a settling delay at each edge, not a true bus bit period. */
volatile uint32_t c3_sccb_half_period_us = 50u;
static void sccb_delay(void)
{
    delay_us(c3_sccb_half_period_us);
}

/**
 * @brief       产生SCCB起始信号
 * @param       无
 * @retval      无
 */
void sccb_start(void)
{
    SCCB_SDA(1);
    SCCB_SCL(1);
    sccb_delay();
    SCCB_SDA(0);    /* START信号: 当SCL为高时, SDA从高变成低, 表示起始信号 */
    sccb_delay();
    SCCB_SCL(0);    /* 钳住SCCB总线，准备发送或接收数据 */
    sccb_delay();
}

/**
 * @brief       产生SCCB停止信号
 * @param       无
 * @retval      无
 */
void sccb_stop(void)
{
    SCCB_SDA(0);    /* STOP信号: 当SCL为高时, SDA从低变成高, 表示停止信号 */
    sccb_delay();
    SCCB_SCL(1);
    sccb_delay();
    SCCB_SDA(1);    /* 发送SCCB总线结束信号 */
    sccb_delay();
}

/**
 * @brief       不产生ACK应答
 * @param       无
 * @retval      无
 */
void sccb_nack(void)
{
    SCCB_SDA(1);    /* SCL 0 -> 1  时 SDA = 1,表示不应答 */
    sccb_delay();
    SCCB_SCL(1);    /* 产生一个时钟 */
    sccb_delay();
    SCCB_SCL(0);
    sccb_delay();
}

/**
 * @brief       SCCB 发送一个字节
 * @param       data: 要发送的数据
 * @retval      无
 */
uint8_t sccb_send_byte(uint8_t data)
{
    uint8_t t, res;
    
    for (t = 0; t < 8; t++)
    {
        SCCB_SDA((data & 0x80) >> 7);   /* 高位先发送 */
        sccb_delay();
        SCCB_SCL(1);
        sccb_delay();
        SCCB_SCL(0);
        data <<= 1;     /* 左移1位,用于下一次发送 */
    }
#if OV_SCCB_TYPE_NOD
    SCCB_SDA_IN();      /*设置SDA为输入*/
    delay_us(50);
#endif
    SCCB_SDA(1);        /* 发送完成, 主机释放SDA线 */
    sccb_delay();
    SCCB_SCL(1);        /* 接收第九位,以判断是否发送成功 */
    sccb_delay();

    if (SCCB_READ_SDA)
    {
        res = 1;        /* SDA=1发送失败，返回1 */
    }
    else 
    {
        res = 0;        /* SDA=0发送成功，返回0 */
    } 

    SCCB_SCL(0);
#if OV_SCCB_TYPE_NOD
    SCCB_SDA_OUT();      /*设置SDA为输出*/
    delay_us(50);
#endif    
    return res;
}

/**
 * @brief       SCCB 读取一个字节
 * @param       无
 * @retval      读取到的数据
 */
uint8_t sccb_read_byte(void)
{
    uint8_t i, receive = 0;
#if C3_SCCB_FLOAT_READ
    /* Read ownership handoff: SCL is LOW after the device+READ ninth clock.
     * Release SDA before switching PG13 to input; preserve all other CRH
     * pin nibbles, including FIFO controls on PG14/PG15. */
    uint32_t pg13_saved;
    SCCB_SDA(1);
    pg13_saved = GPIOG->CRH & (0xFu << 20);
    GPIOG->CRH = (GPIOG->CRH & ~(0xFu << 20)) | (0x4u << 20);
    sccb_delay();
#elif OV_SCCB_TYPE_NOD
    SCCB_SDA_IN();      /*设置SDA为输入*/
    delay_us(50);
#endif
    for (i = 0; i < 8; i++ )    /* 接收1个字节数据 */
    {
        receive <<= 1;          /* 高位先输出,所以先收到的数据位要左移 */
        SCCB_SCL(1);
        sccb_delay();

        if (SCCB_READ_SDA)
        {
            receive++;
        }
        
        SCCB_SCL(0);
        sccb_delay();
    }
#if C3_SCCB_FLOAT_READ
    /* After the eighth data falling edge, release SDA (ODR=1) while
     * still an input. Restore the original open-drain mode for ninth-bit
     * NACK and STOP, performed by the caller. */
    SCCB_SDA(1);
    GPIOG->CRH = (GPIOG->CRH & ~(0xFu << 20)) | pg13_saved;
    sccb_delay();
#elif OV_SCCB_TYPE_NOD
    SCCB_SDA_OUT();      /*设置SDA为输出*/
    delay_us(50);
#endif
    return receive;
}



















/* v0.1.45: Alternative register READ: write 8-bit subaddress then a
 * REPEATED START (without STOP) before the address+read phase. It is a
 * diagnostic/fallback, NOT a claim that the normal STOP/START is wrong.
 * Leave all data and ninth-bit framing to the existing SCCB primitives. */
uint8_t sccb_read_reg_restart(uint8_t device_addr, uint8_t reg)
{
    uint8_t value;
    sccb_start();
    (void)sccb_send_byte((uint8_t)(device_addr & 0xFEu));
    (void)sccb_send_byte(reg);
    sccb_start(); /* repeated START while SCL and SDA are released */
    (void)sccb_send_byte((uint8_t)(device_addr | 0x01u));
    SCCB_SDA(1); /* explicitly release OD SDA for slave-to-master data */
    value = sccb_read_byte();
    sccb_nack();
    sccb_stop();
    return value;
}


/* v0.1.49: read-only instrumented single register read.
 * Preserve exactly nine clock cycles per master-transmitted phase and eight
 * bits per slave-transmitted data phase; the NA bit follows as usual.
 * Only enabled from the dedicated SCCB scan image. Never use this test
 * receiver for production verification/capture without hardware validation.
 *
 * The three samples reveal whether zeros appear during SCL LOW, immediately
 * after the rising edge, or only later during SCL HIGH. This is NOT a logic
 * analyzer and cannot expose unobserved glitches or verify bus voltage.
 */
void sccb_trace_read_reg(uint8_t device_addr, uint8_t reg,
                         uint8_t input_float, c3_sccb_trace_t *trace)
{
    uint32_t pg13_saved = 0u;
    if (!trace) return;
    trace->low = 0u;
    trace->early = 0u;
    trace->late = 0u;

    sccb_start();
    (void)sccb_send_byte((uint8_t)(device_addr & 0xFEu));
    (void)sccb_send_byte(reg);
    sccb_stop();
    sccb_start();
    (void)sccb_send_byte((uint8_t)(device_addr | 0x01u));
    SCCB_SDA(1); /* ODR=1: release OD and prepare safe input-mode restore */

    /* PG13 is configured as OD output in MX_GPIO_Init(). STM32F1 pin
     * control nibble at GPIOG_CRH[23:20]: 0x6=2MHz OD, 0x4=input floating.
     * Change only the PG13 nibble while SCL is LOW. */
    if (input_float) {
        pg13_saved = GPIOG->CRH & (0xFu << 20);
        GPIOG->CRH = (GPIOG->CRH & ~(0xFu << 20)) | (0x4u << 20);
        sccb_delay();
    }

    for (unsigned i = 0u; i < 8u; ++i) {
        trace->low = (uint8_t)((trace->low << 1) |
            ((GPIOG->IDR & GPIO_PIN_13) ? 1u : 0u));
        SCCB_SCL(1);
        trace->early = (uint8_t)((trace->early << 1) |
            ((GPIOG->IDR & GPIO_PIN_13) ? 1u : 0u));
        sccb_delay();
        trace->late = (uint8_t)((trace->late << 1) |
            ((GPIOG->IDR & GPIO_PIN_13) ? 1u : 0u));
        SCCB_SCL(0);
        sccb_delay();
    }

    if (input_float) {
        /* ODR remains high; restoring open drain cannot drive SDA low. */
        GPIOG->CRH = (GPIOG->CRH & ~(0xFu << 20)) | pg13_saved;
        sccb_delay();
    }
    sccb_nack();
    sccb_stop();
}

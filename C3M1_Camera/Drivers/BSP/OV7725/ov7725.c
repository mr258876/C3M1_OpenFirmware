/*
 * @file        ov7725.c
 * @brief       OV7725 驱动代码
 */

#include "sccb.h"
#ifndef C3_PREVIEW_PROBE
#define C3_PREVIEW_PROBE 0
#endif
/* Experiment: in full RGB565 preview/BMP mode, nonfatal only when a
 * static register readback returns 0xFF. Other mismatches remain fatal.
 * C3_PREVIEW_PROBE keeps its existing broader warning-only behavior.
 * Disable with C3_RGB_VERIFY_WARN_ONLY=0 to restore v0.1.25 strict gate. */
#ifndef C3_RGB_VERIFY_WARN_ONLY
#define C3_RGB_VERIFY_WARN_ONLY 1
#endif
/* RGB only: v0.1.27 0x90/0xB0 fixed vertical orientation but user
 * confirms horizontal mirroring. Toggle ONLY COM3[6] (0x40), preserving
 * the now-correct vertical flip and independently testable B/R swap.
 * 0xD0 (no B/R swap) and 0xF0 (B/R swap). RAW8/DNG is untouched. */
#ifndef C3_CAM_COM3_RGB
#define C3_CAM_COM3_RGB 0xD0u
#endif
/* Only enabled in the separately marked experimental RAW build.
 * FF means the SCCB read could not prove success; not proof of success. */
/* 0=exact v0.1.44 SCCB verification, 1=bounded repeated-START
 * fallback only when the legacy read did not match (mostly 0xFF).
 * The expected value still MUST be observed; RAW remains strict. */
#ifndef C3_SCCB_RESTART_VERIFY
#define C3_SCCB_RESTART_VERIFY 1
#endif
#if (C3_SCCB_RESTART_VERIFY != 0) && (C3_SCCB_RESTART_VERIFY != 1)
#error "C3_SCCB_RESTART_VERIFY must be 0 or 1"
#endif
#ifndef C3_RAW_VERIFY_WARN_IF_FF
#define C3_RAW_VERIFY_WARN_IF_FF 0
#endif
#if (C3_RAW_VERIFY_WARN_IF_FF != 0) && (C3_RAW_VERIFY_WARN_IF_FF != 1)
#error "C3_RAW_VERIFY_WARN_IF_FF must be 0 or 1"
#endif
#if (C3_CAM_COM3_RGB != 0xD0u) && (C3_CAM_COM3_RGB != 0xF0u)
#error "C3_CAM_COM3_RGB only supports controlled values 0xD0 and 0xF0"
#endif
/* Sensor-generated test chart. Only enabled in separate read-only probe. */
#ifndef C3_CAM_COLOR_BAR
#define C3_CAM_COLOR_BAR 0
#endif
/* v0.1.55: field-validated RGB565 clock fix.  PLL 4x, CLKRC=0
 * eliminates moving chromatic bands on this C3M1 board at ~29.4 FPS.
 * Apply it at the same place as v0.1.54 T4 live experiment, AFTER the
 * original register table; this preserves that field-tested write order.
 * RAW/DNG has a separate initialization table and is NOT modified.
 * Define C3_RGB_PLL4_DEFAULT=0 for a byte-identical legacy 8x rollback. */
#ifndef C3_RGB_PLL4_DEFAULT
#define C3_RGB_PLL4_DEFAULT 1
#endif
#if (C3_RGB_PLL4_DEFAULT != 0) && (C3_RGB_PLL4_DEFAULT != 1)
#error "C3_RGB_PLL4_DEFAULT must be 0 or 1"
#endif

/* v0.1.52 read-only sensor-bar clock diagnostics.
 * Neither flag is permitted in the write-capable mainline or still capture.
 * Phase: COM10[4]=1 reverses sensor PCLK (AL422B WCK is directly wired).
 * Slow: COM4[7:6]=01 selects 4x PLL (production preview since v0.1.55).
 * The FIFO read path, LCD DMA, color order and SCCB RX are unchanged. */
#ifndef C3_TEST_PCLK_INV
#define C3_TEST_PCLK_INV 0
#endif
#ifndef C3_TEST_PLL4
#define C3_TEST_PLL4 0
#endif
#if ((C3_TEST_PCLK_INV != 0) && (C3_TEST_PCLK_INV != 1)) || ((C3_TEST_PLL4 != 0) && (C3_TEST_PLL4 != 1))
#error "C3_TEST_PCLK_INV and C3_TEST_PLL4 must be 0 or 1"
#endif
/* v0.1.54: field-tested slow clock options can now be tested on a
 * READ-ONLY *live* preview as well as the original sensor color bars.
 * Do not permit PCLK polarity reversal in normal live preview. */
#if C3_TEST_PCLK_INV && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE || !C3_CAM_COLOR_BAR)
#error "PCLK inversion is only supported in a read-only sensor colorbar probe"
#endif
#if C3_TEST_PLL4 && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE)
#error "PLL4 experiment requires a read-only preview probe"
#endif
/* v0.1.53: isolate the sensor clock rate without changing PCLK polarity.
 * 6x PLL tests the speed between v0.1.52 8x (banding) and 4x (improved).
 * 8x PLL with CLKRC=1 divides the sensor's internal clock by another 2;
 * compare to 4x/CLKRC=0 to separate effective rate from PLL setting.
 * Both options are read-only sensor-colorbar diagnostics. */
#ifndef C3_TEST_PLL6
#define C3_TEST_PLL6 0
#endif
#ifndef C3_TEST_CLKDIV2
#define C3_TEST_CLKDIV2 0
#endif
#if ((C3_TEST_PLL6 != 0) && (C3_TEST_PLL6 != 1)) || \
    ((C3_TEST_CLKDIV2 != 0) && (C3_TEST_CLKDIV2 != 1))
#error "C3_TEST_PLL6 and C3_TEST_CLKDIV2 must be 0 or 1"
#endif
#if (C3_TEST_PLL6 + C3_TEST_CLKDIV2 + C3_TEST_PLL4 + C3_TEST_PCLK_INV) > 1
#error "Only one sensor clock diagnostic option may be active at a time"
#endif
#if C3_TEST_PLL6 && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE || !C3_CAM_COLOR_BAR)
#error "PLL6 experiment remains restricted to sensor colorbar mode"
#endif
#if C3_TEST_CLKDIV2 && (!C3_PREVIEW_PROBE || C3_FIFO_PROBE)
#error "CLKRC division experiment requires a read-only preview probe"
#endif
#include "ov7725.h"
#include "c3_zoom.h"
#ifndef C3_RAW_CLKRC
#define C3_RAW_CLKRC 0u
#endif
#if (C3_RAW_CLKRC != 0u) && (C3_RAW_CLKRC != 1u)
#error "C3_RAW_CLKRC must be 0 or 1"
#endif
#include "c3_rgb_timing.h"
#ifndef C3_RAW_FREEZE_AWB
#define C3_RAW_FREEZE_AWB 1
#endif
#include "ov7725cfg.h"
#include "../../SYSTEM/delay/delay.h"

/* Diagnostic telemetry: watch through SWD or read on startup error screen. */
volatile uint32_t c3_ov_last_mid = 0xFFFFu;
volatile uint32_t c3_ov_last_pid = 0xFFFFu;
volatile uint32_t c3_ov_last_bad_reg = 0xFFu;
volatile uint32_t c3_ov_last_bad_phase = 0u; /* 0x8 = readback verification failed */
volatile uint32_t c3_ov_last_verify_expected = 0xFFu;
volatile uint32_t c3_ov_last_verify_actual = 0xFFu;
/* SCCB's 9th bit is *not* an ACK: this telemetry is non-fatal. */
volatile uint32_t c3_ov_last_dc_reg = 0xFFu;
volatile uint32_t c3_ov_last_dc_bits = 0u;
volatile uint32_t c3_ov_verify_warn_mask = 0u;
volatile uint32_t c3_ov_warn_first_reg = 0xFFu;
volatile uint32_t c3_ov_warn_first_expected = 0xFFu;
volatile uint32_t c3_ov_warn_first_actual = 0xFFu;
volatile uint32_t c3_sccb_restart_attempts = 0u;
volatile uint32_t c3_sccb_restart_successes = 0u;
volatile uint32_t c3_sccb_restart_last_reg = 0xFFu;


/**
 * @brief       OV7725 读SCCB寄存器
 * @param       reg : 寄存器地址
 * @retval      读到的寄存器值
 */
/*为什么分两次通信‌？第一次*预约*要读的寄存器，第二次*取数据*，这是协议规定的步骤*/
uint8_t ov7725_read_reg(uint16_t reg)  /*读OV7725的SCCB，要读的寄存器是16位的，返回一个8位结果*/
{
    uint8_t data = 0;  /*先定义一个变量data给初值0，一会用来存读到的数*/
    
    sccb_start();                       /* 通信起始信号，相当于对OV说：我要开始和你说话了！ */
    sccb_send_byte(OV7725_ADDR);        /* 发送通讯地址，就是IIC总线里面的摄像头设备地址比如0X00 */
    sccb_send_byte(reg);                /* 把要读的寄存器地址告诉OV */
    sccb_stop();                        /* 说完上面的话，先停一下。停止信号 */
    
    /* 设置寄存器地址后，才是读 */
    sccb_start();                       /* 起始信号，重新开始通讯 */
    sccb_send_byte(OV7725_ADDR | 0X01); /* 读通讯地址 ，设备地址+1表示要读*/
    data = sccb_read_byte();            /* 读取数据，比如亮度、对比度等等 */
    sccb_nack();                        /* 非应答信号，要终止通信了。好了，我收到了，不用再发了！ */
    sccb_stop();                        /* 停止信号 */
    
    return data;   /*把读到的寄存器值（比如0x80）返回给调用者*/
}

/* Read the same OV7725 register by repeated-START. Used only for an
 * alternative transaction or a read-only A/B diagnosis. The ninth bit
 * on the master-sent address/subaddress is a Don't-Care, not ACK. */
uint8_t ov7725_read_reg_restart(uint8_t reg)
{
    return sccb_read_reg_restart(OV7725_ADDR, reg);
}

/* OV SCCB: the ninth clock following each master-sent byte is a
 * DON'T-CARE bit, NOT a mandatory I2C ACK. The old "checked" reader
 * failed whenever that bit floated high. Here "success" means that a
 * complete read transaction was executed, NOT that the sensor has been
 * independently verified. Check PID and stable writable registers at init. */
uint8_t ov7725_read_reg_checked(uint8_t reg, uint8_t *value)
{
    if (!value) return 0u;
    sccb_start();
    (void)sccb_send_byte(OV7725_ADDR);
    (void)sccb_send_byte(reg);
    sccb_stop();
    sccb_start();
    (void)sccb_send_byte(OV7725_ADDR | 0x01u);
    *value = sccb_read_byte();
    sccb_nack();
    sccb_stop();
    return 1u;
}

/* A three-phase SCCB write: each byte has 8 data bits plus a mandatory
 * ninth clock. Its ninth-bit SDA LEVEL IS NOT AN ACK (OV SCCB Spec 2.1,
 * section 3.2.1.1, page 9). Retain observations for SWD diagnostics,
 * but never declare a write failed on that basis. Actual register effects
 * are validated by stable readback checks after initialization. */
uint8_t ov7725_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t dc_bits = 0u;
    sccb_start();
    if (sccb_send_byte(OV7725_ADDR)) dc_bits |= 1u;
    if (sccb_send_byte(reg)) dc_bits |= 2u;
    if (sccb_send_byte(data)) dc_bits |= 4u;
    sccb_stop();
    if (dc_bits) {
        c3_ov_last_dc_reg = reg;
        c3_ov_last_dc_bits = dc_bits;
    }
    /* Complete bus transaction, NOT positive confirmation of device write. */
    return 0u;
}

/* A static, documented R/W register is suitable for a readback check.
 * Do NOT apply this to AEC/GAIN (may change automatically) or COM7 reset.
 * Retry only the READ once: this does not rewrite the register. */
volatile uint32_t c3_ov_blc_rgb_readback = 0xFFu;
volatile uint32_t c3_ov_blc_raw_init_readback = 0xFFu;
volatile uint32_t c3_ov_blc_raw_capture_readback = 0xFFu;
volatile uint32_t c3_ov_blc_raw_gain_readback = 0xFFu;
static uint8_t blc_last_readback;

static uint8_t verify_static_reg(uint8_t reg, uint8_t expected, uint8_t mask)
{
    uint8_t actual = 0xFFu;
    for (uint8_t retry = 0u; retry < 2u; ++retry) {
        actual = ov7725_read_reg(reg);
        if (reg == COM13) blc_last_readback = actual;
        if (((uint8_t)(actual ^ expected) & mask) == 0u) return 1u;
#if C3_SCCB_RESTART_VERIFY
        /* The alternative transaction is only accepted when it independently
         * matches the EXPECTED bits, not merely because it is non-0xFF.
         * Do not rewrite registers during verification. */
        ++c3_sccb_restart_attempts;
        c3_sccb_restart_last_reg = reg;
        actual = ov7725_read_reg_restart(reg);
        if (reg == COM13) blc_last_readback = actual;
        if (((uint8_t)(actual ^ expected) & mask) == 0u) {
            ++c3_sccb_restart_successes;
            return 1u;
        }
#endif
        delay_ms(2u);
    }
    c3_ov_last_bad_reg = reg;
    c3_ov_last_bad_phase = 8u;
    c3_ov_last_verify_expected = expected;
    c3_ov_last_verify_actual = actual;
    return 0u;
}

/* v178: explicitly verify the BLC enable state and all four target values.
 * RAW and RGB require the defined COM13 bits to match even if other preview
 * registers have an FF-warning policy. Never write FF off as a valid state. */
static uint8_t verify_blc_config(uint8_t raw, volatile uint32_t *snapshot)
{
    uint8_t ok = verify_static_reg(COM13, raw ? C3_RAW_COM13 : C3_RGB_COM13,
                                   C3_COM13_MASK);
    *snapshot = blc_last_readback;
    if (!ok) return 0u;
    if (!verify_static_reg(TGT_B, raw ? 0x80u : 0x7Fu, 0xFFu)) return 0u;
    if (!verify_static_reg(TGT_R, 0x80u, 0xFFu)) return 0u;
    if (!verify_static_reg(TGT_Gb, 0x80u, 0xFFu)) return 0u;
    if (!verify_static_reg(TGT_Gr, 0x80u, 0xFFu)) return 0u;
    if (raw && !verify_static_reg(COM5, 0x00u, 0x80u)) return 0u;
    return 1u;
}

/* After final gain/exposure writes and discarded settling frames, before
 * arming the saved frame. Gain is telemetry, not a new exposure gate. */
uint8_t ov7725_verify_raw_blc_capture(void)
{
    c3_ov_blc_raw_gain_readback = ov7725_read_reg(GAIN);
    return verify_blc_config(1u, &c3_ov_blc_raw_capture_readback);
}

/* Record first failed register as an immutable diagnostic snapshot.
 * SCCB readback returning FF is ambiguous: not proof that the write failed.
 * In the preview test, leave that condition nonfatal, but still require a
 * valid PID and VSYNC before enabling FIFO streaming. */
static void preview_verify_warn(uint8_t reg, uint8_t expected,
                                uint8_t mask, uint32_t warn_bit)
{
    if (verify_static_reg(reg, expected, mask)) return;
    if (c3_ov_verify_warn_mask == 0u) {
        c3_ov_warn_first_reg = reg;
        c3_ov_warn_first_expected = expected;
        c3_ov_warn_first_actual = (uint8_t)c3_ov_last_verify_actual;
    }
    c3_ov_verify_warn_mask |= warn_bit;
}

/* RGB565 experiment ONLY: 0xFF can mean a floating/failed SCCB read.
 * Do not pretend the write was verified; report a warning and let the
 * separate PID + two-VSYNC checks decide whether live preview can start.
 * If readback is any other wrong value, keep the original fatal gate. */
static uint8_t rgb_verify_or_warn_if_ff(uint8_t reg, uint8_t expected,
                                        uint8_t mask, uint32_t warn_bit)
{
    if (verify_static_reg(reg, expected, mask)) return 1u;
    if (c3_ov_last_verify_actual != 0xFFu) return 0u;
    if (c3_ov_verify_warn_mask == 0u) {
        c3_ov_warn_first_reg = reg;
        c3_ov_warn_first_expected = expected;
        c3_ov_warn_first_actual = (uint8_t)c3_ov_last_verify_actual;
    }
    c3_ov_verify_warn_mask |= warn_bit;
    return 1u;
}

/* 初始化 OV7725 */
uint8_t ov7725_init(void)
{
    uint16_t i = 0;
    uint16_t reg = 0;
    c3_ov_last_mid = 0xFFFFu;
    c3_ov_last_pid = 0xFFFFu;
    c3_ov_last_bad_reg = 0xFFu;
    c3_ov_last_bad_phase = 0u;
    c3_ov_last_verify_expected = 0xFFu;
    c3_ov_last_verify_actual = 0xFFu;
    c3_ov_last_dc_reg = 0xFFu;
    c3_ov_last_dc_bits = 0u;
    c3_ov_verify_warn_mask = 0u;
    c3_ov_warn_first_reg = 0xFFu;
    c3_ov_warn_first_expected = 0xFFu;
    c3_ov_warn_first_actual = 0xFFu;
    c3_sccb_restart_attempts = 0u;
    c3_sccb_restart_successes = 0u;
    c3_sccb_restart_last_reg = 0xFFu;

    OV7725_WRST(1);             /* WRST =1 */
    OV7725_RRST(1);             /* RRST =1 */
    OV7725_OE(1);               /* OE =1 */
    OV7725_RCLK(1);             /* RCLK =1 */
    OV7725_WEN(0);              /* Keep FIFO writes off until a capture is armed */

    sccb_init();                /* 初始化SCCB 的IO口 */

    if (ov7725_write_reg(0x12, 0x80))   /* 复位 */
    {
        return 1;
    }

    delay_ms(50);
    reg = ov7725_read_reg(0X1c);    /* 读取厂家ID 高八位 */
    reg <<= 8;
    {
        uint8_t midl = ov7725_read_reg(0X1du);
#if C3_SCCB_RESTART_VERIFY
        /* 7FFF is allowed by the inherited driver, but the documented
         * OV7725 MIDL is A2. Try an independent read if it is FF. */
        if (midl == 0xFFu) {
            ++c3_sccb_restart_attempts;
            c3_sccb_restart_last_reg = 0x1Du;
            uint8_t alt = ov7725_read_reg_restart(0x1Du);
            if (alt == 0xA2u) {
                midl = alt;
                ++c3_sccb_restart_successes;
            }
        }
#endif
        reg |= midl;
    }
    c3_ov_last_mid = reg;

    if ((reg != OV7725_MID) && (reg != OV7725_MID1))    /* MID不正确 ? */
    {
        /* Unknown sensor ID: do not try UART before USART init. */
        return 1;
    }

    reg = ov7725_read_reg(0X0a);    /* 读取厂家ID 高八位 */
    reg <<= 8;
    reg |= ov7725_read_reg(0X0b);   /* 读取厂家ID 低八位 */
    c3_ov_last_pid = reg;

    if (reg != OV7725_PID)          /* PID不正确 ? */
    {
        /* Unknown sensor ID: caller reports error on LCD. */
        return 2;
    }

#if C3_ZOOM_AE_LOCK
    /* Do this before the first table write as well: reset defaults can
     * permit auto exposure even when COM5's night bit is clear. */
    if(ov7725_write_reg(COM8,0xF8u)||ov7725_write_reg(COM5,0u))return 3u;
#endif
    /* 初始化 OV7725,采用QVGA分辨率(320*240) */
    for (i = 0; i < sizeof(ov7725_init_reg_tb1) / sizeof(ov7725_init_reg_tb1[0]); i++)
    {
#if C3_ZOOM_AE_LOCK
        /* RGB initialization must not run auto exposure/night mode before
         * the actual P/S/M (or magnification hold) is applied. */
        uint8_t addr=ov7725_init_reg_tb1[i][0];
        uint8_t value=ov7725_init_reg_tb1[i][1];
        if(addr==COM8) value=0xF8u;
        if(addr==COM5) value=0u;
        if(ov7725_write_reg(addr,value))return 3;
#else
        if (ov7725_write_reg(ov7725_init_reg_tb1[i][0],
                             ov7725_init_reg_tb1[i][1])) return 3;
#endif
    }

    /* Apply RGB565 direction AFTER the old table's COM3=0x50.
     * v27 0x90/0xB0 left a horizontal mirror; v28 re-enables COM3[6],
     * yielding 0xD0/0xF0.  COM3[5] tests RGB B/R sequence separately.
     * RAW8/DNG setup is unchanged. SCCB readback remains unverified. */
    (void)ov7725_write_reg(COM3, (uint8_t)(C3_CAM_COM3_RGB |
                                (C3_CAM_COLOR_BAR ? 0x01u : 0x00u)));

#if C3_TEST_PLL4 || (C3_RGB_PLL4_DEFAULT && !C3_TEST_PLL6 && \
                       !C3_TEST_CLKDIV2 && !C3_TEST_PCLK_INV)
    /* v0.1.55 mainline: same post-table COM4 write as field-tested T4.
     * Explicit v0.1.53 clock experiments still select their own PLL/CLKRC.
     * The separate RAW table remains COM4=0x41, CLKRC=0x01. */
    (void)ov7725_write_reg(COM4, 0x41u);
#endif
#if C3_TEST_PLL6
    /* OV7725 COM4[7:6]=10 = PLL 6x; original COM4[5:0] kept at 0x01. */
    (void)ov7725_write_reg(COM4, 0x81u);
#endif
#if C3_TEST_CLKDIV2
    /* Original table: PLL 8x, CLKRC=0. CLKRC=1 reduces the internally
     * derived clock nominally by 2, without changing PLL multiplication.
     * This may affect exposure/frame cadence; strictly a colorbar probe. */
    (void)ov7725_write_reg(CLKRC, 0x01u);
#endif
#if C3_TEST_PCLK_INV
    /* Reverses OV7725 PCLK without changing GPIO or the FIFO read loop.
     * COM10 is otherwise left at its sensor reset/default value. */
    (void)ov7725_write_reg(COM10, 0x10u);
#endif

    if (!verify_blc_config(0u, &c3_ov_blc_rgb_readback)) return 4u;

    /* Experimental PREVIEW only: static register readback is telemetry,
     * not a fatal gate. It can be wrong if SCCB reads lag/mis-sample, and
     * we've observed 0x94 returning FF on hardware. The separate PID and
     * VSYNC checks in c3_camera.c remain mandatory.
     * Do NOT extend this relaxation to RAW8 recording. */
#if C3_PREVIEW_PROBE
    preview_verify_warn(MTX1, 0xB0u, 0xFFu, C3_OV_WARN_MTX1);
    preview_verify_warn(MTX2, 0x9Du, 0xFFu, C3_OV_WARN_MTX2);
    preview_verify_warn(DSP_Ctrl4, 0x00u, 0x03u, C3_OV_WARN_DSP4);
#elif C3_RGB_VERIFY_WARN_ONLY
    /* Mainline RGB565 only: warn for FF, stop for other wrong values. */
    if (!rgb_verify_or_warn_if_ff(MTX1, 0xB0u, 0xFFu, C3_OV_WARN_MTX1)) return 4u;
    if (!rgb_verify_or_warn_if_ff(MTX2, 0x9Du, 0xFFu, C3_OV_WARN_MTX2)) return 4u;
    if (!rgb_verify_or_warn_if_ff(DSP_Ctrl4, 0x00u, 0x03u, C3_OV_WARN_DSP4)) return 4u;
#else
    /* Original v0.1.25 strict gate. */
    if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;
    if (!verify_static_reg(MTX2, 0x9Du, 0xFFu)) return 4u;
    if (!verify_static_reg(DSP_Ctrl4, 0x00u, 0x03u)) return 4u;
#endif
    return 0;
}


/*这段代码用于测试RAW格式*/
uint8_t ov7725_RAW_init(void)
{
    c3_ov_blc_raw_init_readback = 0xFFu;
    c3_ov_blc_raw_capture_readback = 0xFFu;
    c3_ov_blc_raw_gain_readback = 0xFFu;
    /* Clear preview warnings so RAW warnings genuinely belong to this shot.
     * Preserve last readback error until a new register is checked. */
    c3_ov_last_bad_reg = 0xFFu;
    c3_ov_last_bad_phase = 0u;
    c3_ov_last_verify_expected = 0xFFu;
    c3_ov_last_verify_actual = 0xFFu;
    c3_ov_verify_warn_mask = 0u;
    c3_ov_warn_first_reg = 0xFFu;
    c3_ov_warn_first_expected = 0xFFu;
    c3_ov_warn_first_actual = 0xFFu;
    OV7725_WEN(0);
    sccb_init();
    if (ov7725_write_reg(COM7, 0x80)) return 1;
    delay_ms(50);
    for (uint16_t i=0;i<sizeof(ov7725_init_reg_tbRAW)/sizeof(ov7725_init_reg_tbRAW[0]);i++) {
#if C3_RAW_CLKRC == 1 && !C3_RAW_FREEZE_AWB
        /* Literal legacy sequence for byte-exact rollback and slow RAW. */
        if (ov7725_write_reg(ov7725_init_reg_tbRAW[i][0],
                             ov7725_init_reg_tbRAW[i][1])) return 2;
#else
        uint8_t reg = ov7725_init_reg_tbRAW[i][0];
        uint8_t val = ov7725_init_reg_tbRAW[i][1];
        /* RAW CLKRC=0 is experimental: PLL4 and other settings unchanged. */
        if (reg == CLKRC) val=(uint8_t)C3_RAW_CLKRC;
#if C3_RAW_FREEZE_AWB
        if (reg == COM8) val=0x00u; /* no RAW auto white balance */
#endif
        if (ov7725_write_reg(reg,val)) return 2;
#endif
    }
    if (!verify_blc_config(1u, &c3_ov_blc_raw_init_readback)) return 4u;
#if C3_RAW_VERIFY_WARN_IF_FF
    /* Experimental RAW8 ONLY: continue for FF readbacks while retaining
     * warning telemetry. Other mismatches remain fatal. Sensor output
     * format and resulting DNG data remain UNVERIFIED on physical hardware. */
    if (!rgb_verify_or_warn_if_ff(MTX1, 0xB0u, 0xFFu, C3_OV_WARN_MTX1)) return 4u;
    if (!rgb_verify_or_warn_if_ff(DSP_Ctrl4, 0x02u, 0x03u, C3_OV_WARN_DSP4)) return 4u;
#else
    if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;
    if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;
#endif
    return 0;
}

#ifndef C3_FAST_RAW_SWITCH
#define C3_FAST_RAW_SWITCH 0
#endif
#if C3_FAST_RAW_SWITCH
#if (C3_RAW_CLKRC != 0u) || (C3_RGB_PLL4_DEFAULT != 1)
#error "RAW differential switch needs preview PLL4 CLKRC0 and RAW CLKRC0"
#endif
/* Only skip RAW registers with known invariant values in the production
 * RGB565 preview (not exposure/AE/AGC/window/format-dependent state).
 * The RAW table is still authoritative. This deliberately does not change
 * the FIFO PCLK read edge or anything in DNG saving. */
static uint8_t raw_same_as_preview(uint8_t reg, uint8_t val)
{
    switch (reg) {
    case COM4: return (uint8_t)(val == 0x41u);
    #if C3_SHUTTER_DYNAMIC_TIMING
    case CLKRC: return 0u; /* preview 4X/8000 can be CLKRC=0x40 */
#else
    case CLKRC: return (uint8_t)(val == 0x00u);
#endif
    case HREF: return (uint8_t)(val == 0x00u);
    case DSP_Ctrl2: case DSP_Ctrl3: return (uint8_t)(val == 0x00u);
    case BDBase: return (uint8_t)(val == 0x7Fu);
    case BDMStep: return (uint8_t)(val == 0x03u);
    case AWBCtrl3: return (uint8_t)(val == 0xAAu);
    case EDGE1: return (uint8_t)(val == 0x08u);
    case DNSOff: return (uint8_t)(val == 0x01u);
    case EDGE2: return (uint8_t)(val == 0x03u);
    case EDGE3: return (uint8_t)(val == 0x00u);
    case MTX1: return (uint8_t)(val == 0xB0u);
    case MTX2: return (uint8_t)(val == 0x9Du);
    case MTX3: return (uint8_t)(val == 0x13u);
    case MTX4: return (uint8_t)(val == 0x16u);
    case MTX5: return (uint8_t)(val == 0x7Bu);
    case MTX6: return (uint8_t)(val == 0x91u);
    case MTX_Ctrl: return (uint8_t)(val == 0x1Eu);
    case BRIGHT: return (uint8_t)(val == 0x08u);
    case CNST: return (uint8_t)(val == 0x20u);
    case UVADJ0: return (uint8_t)(val == 0x81u);
    case USAT: case VSAT: return (uint8_t)(val == 0x65u);
    case GAM1: return (uint8_t)(val == 0x0Cu);
    case GAM2: return (uint8_t)(val == 0x16u);
    case GAM3: return (uint8_t)(val == 0x2Au);
    case GAM4: return (uint8_t)(val == 0x4Eu);
    case GAM5: return (uint8_t)(val == 0x61u);
    case GAM6: return (uint8_t)(val == 0x6Fu);
    case GAM7: return (uint8_t)(val == 0x7Bu);
    case GAM8: return (uint8_t)(val == 0x86u);
    case GAM9: return (uint8_t)(val == 0x8Eu);
    case GAM10: return (uint8_t)(val == 0x97u);
    case GAM11: return (uint8_t)(val == 0xA4u);
    case GAM12: return (uint8_t)(val == 0xAFu);
    case GAM13: return (uint8_t)(val == 0xC5u);
    case GAM14: return (uint8_t)(val == 0xD7u);
    case GAM15: return (uint8_t)(val == 0xE8u);
    case SLOP: return (uint8_t)(val == 0x20u);
    default: return 0u;
    }
}
uint8_t ov7725_RAW_fast_init(void)
{
    c3_ov_blc_raw_init_readback = 0xFFu;
    c3_ov_blc_raw_capture_readback = 0xFFu;
    c3_ov_blc_raw_gain_readback = 0xFFu;
    /* Input must be a known-good RGB565 preview. No COM7 software reset:
     * explicitly restore COM3=0x10 (OV7725 datasheet reset default) so the
     * retained RGB COM3=0xD0 does not rotate RAW or change CFA phase.
     * Shut AEC/AGC off BEFORE modifying mode/window to avoid racing ISP. */
    c3_ov_verify_warn_mask = 0u;
    c3_ov_last_bad_reg = 0xFFu;
    OV7725_WEN(0);
#if C3_RAW_FREEZE_AWB
    (void)ov7725_write_reg(COM8, 0x00u); /* AEC/AGC/AWB all frozen before format swap */
#else
    (void)ov7725_write_reg(COM8, 0x02u);
#endif
    (void)ov7725_write_reg(COM6, 0x42u); /* Disable auto-window on COM7. */
    (void)ov7725_write_reg(COM3, 0x10u);
    for (uint16_t i = 0u; i < sizeof(ov7725_init_reg_tbRAW)/sizeof(ov7725_init_reg_tbRAW[0]); ++i) {
        uint8_t reg = ov7725_init_reg_tbRAW[i][0];
        uint8_t val = ov7725_init_reg_tbRAW[i][1];
        if (reg == CLKRC) val = (uint8_t)C3_RAW_CLKRC;
#if C3_RAW_FREEZE_AWB
        if (reg == COM8) val = 0x00u; /* RAW mode must not restart AWB */
#endif
        if (!raw_same_as_preview(reg, val))
            (void)ov7725_write_reg(reg, val);
    }
    /* Any failed readback forces exact full-reset RAW initialization. A
     * correct register read cannot prove the FIFO Bayer frame; field-test
     * against v0.1.65 before enabling outside the experimental build. */
    if (!verify_blc_config(1u, &c3_ov_blc_raw_init_readback)) return 4u;
    if (!verify_static_reg(COM7, 0x13u, 0xFFu)) return 4u;
    if (!verify_static_reg(COM3, 0x10u, 0xF8u)) return 4u;
    if (!verify_static_reg(HSIZE, 0xA1u, 0xFFu)) return 4u;
    if (!verify_static_reg(VSIZE, 0xF0u, 0xFFu)) return 4u;
    if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;
    if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;
    return 0u;
}
#endif

#ifndef C3_FAST_RGB_RESTORE
#define C3_FAST_RGB_RESTORE 0
#endif
#if C3_FAST_RAW_SWITCH && C3_FAST_RGB_RESTORE
/* The RAW table is the authoritative end-state after a successful nonreset
 * RGB->RAW transition. Skip only identical nonvolatile entries; never skip
 * COM7, COM8, COM6, active AEC/AGC, window, or AWB control. */
static uint8_t rgb_matches_raw_static(uint8_t reg, uint8_t value)
{
    switch (reg) {
    case COM7: case COM8: case COM4: case COM6: case CLKRC:
    case HSTART: case HSIZE: case VSTRT: case VSIZE:
    case HOutSize: case VOutSize: case EXHCL: case TGT_B:
    case COM13: case TGT_R: case TGT_Gb: case TGT_Gr:
    case AEW: case AEB: case VPT:
    case FixGain: case AWB_Ctrl0: case DSP_Ctrl1: case DSP_Ctrl4:
    case COM9: case COM5: return 0u;
    default: break;
    }
    /* Exhaustive current RGB table inspection against RAW table values.
     * If a register appears more than once, only the LAST RAW write wins. */
    uint8_t found=0u, last=0u;
    for (uint16_t i=0u; i<sizeof(ov7725_init_reg_tbRAW)/
                                 sizeof(ov7725_init_reg_tbRAW[0]); ++i) {
        if (ov7725_init_reg_tbRAW[i][0] == reg) {
            found=1u;
            last=ov7725_init_reg_tbRAW[i][1];
        }
    }
    return (uint8_t)(found && last == value);
}

uint8_t ov7725_RGB_fast_restore(void)
{
    /* Called only after a successfully saved/discarded RAW frame, from the
     * known no-reset RAW state. FIFO producer has been disabled. AEC/AGC/AWB
     * remain OFF until the caller restores their preview snapshots and calls
     * c3_exposure_apply(PREVIEW). In particular, do not write COM7=0x80. */
    OV7725_WEN(0);
    (void)ov7725_write_reg(COM8,0xF8u); /* keep all 3 auto controls OFF */
    (void)ov7725_write_reg(COM6,0x42u); /* no auto-window on COM7 change */
    for (uint16_t i=0u; i<sizeof(ov7725_init_reg_tb1)/
                               sizeof(ov7725_init_reg_tb1[0]); ++i) {
        const uint8_t reg=ov7725_init_reg_tb1[i][0];
        const uint8_t value=
#if C3_ZOOM_AE_LOCK
            reg==COM5?0u:
#endif
            ov7725_init_reg_tb1[i][1];
        if (reg == COM8 || reg == COM4) continue;
        if (!rgb_matches_raw_static(reg,value))
            (void)ov7725_write_reg(reg,value);
    }
    (void)ov7725_write_reg(COM3,(uint8_t)C3_CAM_COM3_RGB);
    /* RGB and RAW both already use PLL 4x / CLKRC 0. Never generate the
     * transient 8x PLL setting from the original RGB initialization table. */
    if (!verify_blc_config(0u, &c3_ov_blc_rgb_readback)) return 4u;
    if (!verify_static_reg(COM7,0x06u,0xFFu)) return 4u;
    /* COM3[7] is reserved/variant-specific on public OV7725 revisions.
     * Read back only defined B/R, mirror, output-sequence bits; avoid an
     * automatic full reset solely because that bit reads differently. */
    if (!verify_static_reg(COM3,(uint8_t)C3_CAM_COM3_RGB,0x78u)) return 4u;
    if (!verify_static_reg(DSP_Ctrl4,0x00u,0x03u)) return 4u;
    if (!verify_static_reg(COM4,0x41u,0xC0u)) return 4u;
    if (!verify_static_reg(CLKRC,0x00u,
#if C3_SHUTTER_DYNAMIC_TIMING
                           0x7Fu
#else
                           0x3Fu
#endif
                           )) return 4u;
    return 0u; /* still paused; caller re-enables preview after restoring WB */
}
#endif

/**
 * @brief       设置图像输出窗口
 * @param       width   : 输出图像宽度, <=320
 * @param       height  : 输出图像高度, <=240
 * @param       mode    : 输出模式
 *   @arg       0: QVGA输出模式, 可视范围大, 最大输出分辨率: 320*240
 *   @arg       1: VGA输出模式 , 可视范围小, 最大输出分辨率: 320*240
 * @retval      无
 */
/* RGB565 preview: sensor hardware crops VGA centre to 320x240 when
 * magnifier is active. There is NO software interpolation. Keep the sensor
 * output geometry identical in both modes so AL422B frame size is stable.
 * COM7 is written first (COM6 may auto-update the window on mode changes),
 * followed by the explicit window and output registers. */
uint8_t ov7725_preview_window_set(uint8_t zoom4x)
{
    c3_zoom_window_t w = c3_zoom_window(zoom4x);
    uint8_t err = 0u;
    err |= ov7725_write_reg(COM7, w.com7);
    err |= ov7725_write_reg(HSTART, w.hstart);
    err |= ov7725_write_reg(HSIZE, w.hsize);
    err |= ov7725_write_reg(VSTRT, w.vstrt);
    err |= ov7725_write_reg(VSIZE, w.vsize);
    err |= ov7725_write_reg(HREF, w.href);
    err |= ov7725_write_reg(HOutSize, w.houtsize);
    err |= ov7725_write_reg(VOutSize, w.voutsize);
    /* HOutSize/ VOutSize LSBs are zero for 320x240; preserve dummy-pixel
     * control bits (0xF8) rather than OR-ing new LSBs into old ones. */
    /* This register is read-modify-written. An SCCB read error that
     * returns FF must not silently manufacture a 0xF8 dummy-pixel value.
     * The window sizes/origins above have zero LSBs. If neither reader
     * produces reliable data, leave EXHCH unchanged and keep preview. */
    {
        uint8_t exhch = ov7725_read_reg(EXHCH);
#if C3_SCCB_RESTART_VERIFY
        if (exhch == 0xFFu) {
            ++c3_sccb_restart_attempts;
            c3_sccb_restart_last_reg = EXHCH;
            exhch = ov7725_read_reg_restart(EXHCH);
            if (exhch != 0xFFu) ++c3_sccb_restart_successes;
        }
#endif
        if (exhch != 0xFFu)
            err |= ov7725_write_reg(EXHCH, (uint8_t)(exhch & 0xF8u));
    }
    return err ? 1u : 0u;
}

uint8_t ov7725_window_set(uint16_t width, uint16_t height)
{
    uint8_t raw, temp, err = 0;
    uint16_t sx, sy;

	sx = (320 - width) / 2;
	sy = (240 - height) / 2;

	err |= ov7725_write_reg(COM7, 0x46);       /* 设置为QVGA模式 */
	err |= ov7725_write_reg(HSTART, 0x3f);     /* 水平起始位置 */
	err |= ov7725_write_reg(HSIZE, 0x50);      /* 水平尺寸 */
	err |= ov7725_write_reg(VSTRT, 0x03);      /* 垂直起始位置 */
	err |= ov7725_write_reg(VSIZE, 0x78);      /* 垂直尺寸 */
	err |= ov7725_write_reg(HREF,  0x00);
	err |= ov7725_write_reg(HOutSize, 0x50);   /* 输出尺寸 */
	err |= ov7725_write_reg(VOutSize, 0x78);   /* 输出尺寸 */

    raw = ov7725_read_reg(HSTART);
    temp = raw + (sx >> 2);                 /* sx高8位存在HSTART,低2位存在HREF[5:4] */
    err |= ov7725_write_reg(HSTART, temp);
    err |= ov7725_write_reg(HSIZE, width >> 2);    /* width高8位存在HSIZE,低2位存在HREF[1:0] */

    raw = ov7725_read_reg(VSTRT);
    temp = raw + (sy >> 1);                 /* sy高8位存在VSTRT,低1位存在HREF[6] */
    err |= ov7725_write_reg(VSTRT, temp);
    err |= ov7725_write_reg(VSIZE, height >> 1);   /* height高8位存在VSIZE,低1位存在HREF[2] */

    raw = ov7725_read_reg(HREF);
    temp = ((sy & 0x01) << 6) | ((sx & 0x03) << 4) | ((height & 0x01) << 2) | (width & 0x03) | raw;
    err |= ov7725_write_reg(HREF, temp);

    err |= ov7725_write_reg(HOutSize, width >> 2);
    err |= ov7725_write_reg(VOutSize, height >> 1);

    raw = ov7725_read_reg(EXHCH);
    temp = (raw | (width & 0x03) | ((height & 0x01) << 2));
    err |= ov7725_write_reg(EXHCH, temp);
    return err ? 1 : 0;
}

/**
 * @brief       OV7725 灯光模式设置
 * @param       mode : 0~5, 灯光模式
 *   @arg       0: 自动
 *   @arg       1: 日光sunny
 *   @arg       2: 阴天cloudy
 *   @arg       3: 办公室office
 *   @arg       4: 家里home
 *   @arg       5: 夜晚night
 * @retval      无
 */
void ov7725_light_mode(uint8_t mode)
{
    switch (mode)
    {
        case 0: /* auto, 自动模式 */
            ov7725_write_reg(0x13, 0xff); /* AWB on */
            ov7725_write_reg(0x0e, 0x65);
            ov7725_write_reg(0x2d, 0x00);
            ov7725_write_reg(0x2e, 0x00);
            break;

        case 1: /* sunny, 晴天 */
            ov7725_write_reg(0x13, 0xfd); /* AWB off */
            ov7725_write_reg(0x01, 0x5a);
            ov7725_write_reg(0x02, 0x5c);
            ov7725_write_reg(0x0e, 0x65);
            ov7725_write_reg(0x2d, 0x00);
            ov7725_write_reg(0x2e, 0x00);
            break;

        case 2: /* cloudy, 阴天 */
            ov7725_write_reg(0x13, 0xfd); /* AWB off */
            ov7725_write_reg(0x01, 0x58);
            ov7725_write_reg(0x02, 0x60);
            ov7725_write_reg(0x0e, 0x65);
            ov7725_write_reg(0x2d, 0x00);
            ov7725_write_reg(0x2e, 0x00);
            break;

        case 3: /* office, 办公室 */
            ov7725_write_reg(0x13, 0xfd); /* AWB off */
            ov7725_write_reg(0x01, 0x84);
            ov7725_write_reg(0x02, 0x4c);
            ov7725_write_reg(0x0e, 0x65);
            ov7725_write_reg(0x2d, 0x00);
            ov7725_write_reg(0x2e, 0x00);
            break;

        case 4: /* home, 家里 */
            ov7725_write_reg(0x13, 0xfd); /* AWB off */
            ov7725_write_reg(0x01, 0x96);
            ov7725_write_reg(0x02, 0x40);
            ov7725_write_reg(0x0e, 0x65);
            ov7725_write_reg(0x2d, 0x00);
            ov7725_write_reg(0x2e, 0x00);
            break;

        case 5: /* night, 夜晚 */
            ov7725_write_reg(0x13, 0xff); /* AWB on */
            ov7725_write_reg(0x0e, 0xe5);
            break;
    }
}

/**
 * @brief       OV7725 色彩饱和度设置
 * @param       set : 0~8, 代表色彩饱和度 -4 ~ 4.
 * @retval      无
 */
void ov7725_color_saturation(uint8_t sat)
{
    ov7725_write_reg(USAT, sat << 4);
    ov7725_write_reg(VSAT, sat << 4);
}

/**
 * @brief       OV7725 亮度设置
 * @param       bright : 0~8, 代表亮度 -4 ~ 4.
 * @retval      无
 */
void ov7725_brightness(uint8_t bright)
{
    uint8_t bright_value, sign;

    switch (bright)
    {
        case 8:
            bright_value = 0x48;
            sign = 0x06;
            break;

        case 7:
            bright_value = 0x38;
            sign = 0x06;
            break;

        case 6:
            bright_value = 0x28;
            sign = 0x06;
            break;

        case 5:
            bright_value = 0x18;
            sign = 0x06;
            break;

        case 4:
            bright_value = 0x08;
            sign = 0x06;
            break;

        case 3:
            bright_value = 0x08;
            sign = 0x0e;
            break;

        case 2:
            bright_value = 0x18;
            sign = 0x0e;
            break;

        case 1:
            bright_value = 0x28;
            sign = 0x0e;
            break;

        case 0:
            bright_value = 0x38;
            sign = 0x0e;
            break;
    }

    ov7725_write_reg(BRIGHT, bright_value);
    ov7725_write_reg(SIGN, sign);
}

/**
 * @brief       OV7725 对比度设置
 * @param       contrast : 0~8, 代表对比度 -4 ~ 4.
 * @retval      无
 */
void ov7725_contrast(uint8_t contrast)
{
    ov7725_write_reg(CNST, (0x30 - (8 - contrast) * 4));
}

/**
 * @brief       OV7725 特效设置
 * @param       eft : 0~6, 特效效果
 *   @arg       0: 正常
 *   @arg       1: 负片
 *   @arg       2: 黑白
 *   @arg       3: 偏红
 *   @arg       4: 偏绿
 *   @arg       5: 偏蓝
 *   @arg       6: 复古
 * @retval      无
 */
void ov7725_special_effects(uint8_t eft)
{
    switch (eft)
    {
        case 0: /* 正常 */
            ov7725_write_reg(0xa6, 0x06);   /* TSLB设置 */
            ov7725_write_reg(0x60, 0x80);   /* MANV,手动V值 */
            ov7725_write_reg(0x61, 0x80);   /* MANU,手动U值 */
            break;

        case 1: /* 负片 */
            ov7725_write_reg(0xa6, 0x46);
            break;

        case 2: /* 黑白 */
            ov7725_write_reg(0xa6, 0x26);
            ov7725_write_reg(0x60, 0x80);
            ov7725_write_reg(0x61, 0x80);
            break;

        case 3: /* 偏红 */
            ov7725_write_reg(0xa6, 0x1e);
            ov7725_write_reg(0x60, 0x80);
            ov7725_write_reg(0x61, 0xc0);
            break;

        case 4: /* 偏绿 */
            ov7725_write_reg(0xa6, 0x1e);
            ov7725_write_reg(0x60, 0x60);
            ov7725_write_reg(0x61, 0x60);
            break;

        case 5: /* 偏蓝 */
            ov7725_write_reg(0xa6, 0x1e);
            ov7725_write_reg(0x60, 0xa0);
            ov7725_write_reg(0x61, 0x40);
            break;

        case 6: /* 复古 */
            ov7725_write_reg(0xa6, 0x1e);
            ov7725_write_reg(0x60, 0x40);
            ov7725_write_reg(0x61, 0xa0);
            break;

    }
}












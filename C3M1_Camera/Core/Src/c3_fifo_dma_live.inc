/* Four-row FIFO DMA, CPU composition and LCD DMA pipeline.
 * Peripheral setup and status polling occur outside active FIFO transfers. */
#include "c3_preview_dma_render.h"
#define C3_LIVE_ROWS C3_FIFO_DMA_LIVE_ROWS
#define C3_LIVE_BYTES (C3_FD_BYTES*C3_LIVE_ROWS)
#define C3_LIVE_BLOCKS (C3_FD_ROWS/C3_LIVE_ROWS)
_Static_assert(C3_FD_ROWS%C3_LIVE_ROWS==0 && C3_LIVE_BYTES<65536u,"exact block geometry");
static uint16_t live_raw[2][C3_LIVE_BYTES] __attribute__((aligned(4)));
static uint8_t live_out[2][C3_LIVE_BYTES] __attribute__((aligned(4)));
static uint32_t live_mapr_saved,live_crl_saved;
static uint8_t live_active,live_disabled;
volatile uint32_t c3_live_frames,c3_live_errors,c3_live_last_error,c3_live_last_block,c3_live_last_cycles;
static const c3_fd_plan_t live_plan={0,C3_FIFO_DMA_LIVE_TICKS,C3_FIFO_DMA_LIVE_TICKS-2u,1u};
static void live_wait(uint32_t cycles)
{
    uint32_t t=DWT->CYCCNT;
    while((uint32_t)(DWT->CYCCNT-t)<cycles) __NOP();
}
static void live_session_end(void)
{
    if(!live_active) return;
    TIM3->CR1=0u; TIM3->DIER=0u;
    TIM4->CR1=0u; TIM4->DIER=0u;
    DMA1_Channel2->CCR=0u;
    DMA1->IFCR=DMA_IFCR_CGIF2;
    __DSB();
    /* Restore GPIO with preloaded HIGH. Failed transfer may have lost a
     * read edge; never continue reading the same pointer after a failure. */
    GPIOB->BSRR=GPIO_PIN_4;
    GPIOB->CRL=(GPIOB->CRL & ~(0xfu<<16u)) | (live_crl_saved & (0xfu<<16u));
    TIM3->CCER=0u;
    TIM3->SMCR=0u; TIM4->SMCR=0u;
    AFIO->MAPR=(AFIO->MAPR & ~(AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG)) |
               (live_mapr_saved & AFIO_MAPR_TIM3_REMAP) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    __DSB();
    live_active=0u;
}
static uint8_t live_session_begin(const c3_fd_plan_t *p)
{
    if(live_active || !p->ticks || HAL_RCC_GetHCLKFreq()!=72000000u ||
       HAL_RCC_GetPCLK1Freq()!=36000000u) return 0u;
    RCC->APB1ENR|=RCC_APB1ENR_TIM3EN|RCC_APB1ENR_TIM4EN;
    RCC->AHBENR|=RCC_AHBENR_DMA1EN;
    (void)RCC->APB1ENR; (void)RCC->AHBENR;
    __DSB();
    live_mapr_saved=AFIO->MAPR; live_crl_saved=GPIOB->CRL;
    live_active=1u;
    /* Exclusive preview session: neither timer nor DMA1_CH2 is used by still capture. */
    TIM3->CR1=0u; TIM3->CR2=0u; TIM3->SMCR=0u; TIM3->DIER=0u;
    TIM3->CCER=0u; TIM3->CCMR1=TIM_CCMR1_OC1M_2|TIM_CCMR1_OC1M_1; /* PWM1 */
    TIM3->CCMR2=0u; /* CH3 output compare frozen; compare event still occurs */
    TIM3->PSC=0u; TIM3->ARR=p->ticks-1u; TIM3->CCR1=p->high_ticks;
    TIM3->CCR3=p->sample_tick; TIM3->EGR=TIM_EGR_UG; TIM3->SR=0u;
    TIM3->CCER=TIM_CCER_CC1E; /* RCLK HIGH at CNT=0 */
    TIM4->CR1=0u; TIM4->CR2=0u; TIM4->SMCR=0u; TIM4->DIER=0u;
    TIM4->CCER=0u; /* internal OC1REF only, no physical pin */
    TIM4->CCMR1=TIM_CCMR1_OC1M_2|TIM_CCMR1_OC1M_1;
    TIM4->CCMR2=0u; TIM4->PSC=0u; TIM4->ARR=0xffffu;
    TIM4->CCR1=C3_LIVE_BYTES; TIM4->EGR=TIM_EGR_UG; TIM4->SR=0u;
    /* F1 SWJ_CFG is not reliably readable. Always preserve SWD, disable JTAG. */
    AFIO->MAPR=(live_mapr_saved & ~(AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG)) |
               AFIO_MAPR_TIM3_REMAP_PARTIALREMAP|AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    GPIOB->BSRR=GPIO_PIN_4;
    /* PB4 only: AF push-pull 50MHz, PB3 WEN remains ordinary GPIO. */
    GPIOB->CRL=(live_crl_saved & ~(0xfu<<16u)) | (0xbu<<16u);
    __DSB();
    if(!(GPIOB->IDR&GPIO_PIN_4)) { live_session_end(); return 0u; }
    return 1u;
}

static void live_fifo_prepare(unsigned block)
{
    /* Break feedback BEFORE UG: a software update must never be counted
     * as a real RCLK edge. All requests disabled while programming. */
    TIM3->CR1=0u; TIM3->DIER=0u;
    TIM4->CR1=0u; TIM4->SMCR=0u;
    TIM3->SMCR=0u; TIM3->CR2=0u; TIM4->CR2=0u;
    TIM3->EGR=TIM_EGR_UG; TIM3->SR=0u;
    TIM4->EGR=TIM_EGR_UG; TIM4->SR=0u;
    DMA1_Channel2->CCR=0u; DMA1->IFCR=DMA_IFCR_CGIF2;
    DMA1_Channel2->CPAR=(uint32_t)(uintptr_t)&GPIOG->IDR;
    DMA1_Channel2->CMAR=(uint32_t)(uintptr_t)live_raw[c3_live_slot(block)];
    DMA1_Channel2->CNDTR=C3_LIVE_BYTES;
    /* 32-bit peripheral access, 16-bit memory (LSBs); no PINC/CIRC/MEM2MEM. */
    DMA1_Channel2->CCR=DMA_CCR_PSIZE_1|DMA_CCR_MSIZE_0|DMA_CCR_MINC|
                        DMA_CCR_PL_0|DMA_CCR_PL_1|DMA_CCR_EN;
    TIM3->CR2=TIM_CR2_MMS_1; /* TRGO=update at end of each period */
    TIM4->CR2=TIM_CR2_MMS_2; /* TRGO=OC1REF */
    TIM4->SMCR=TIM_SMCR_TS_1|TIM_SMCR_SMS_2|TIM_SMCR_SMS_1|TIM_SMCR_SMS_0;
    TIM3->SMCR=TIM_SMCR_TS_1|TIM_SMCR_TS_0|TIM_SMCR_SMS_2|TIM_SMCR_SMS_0;
    TIM3->DIER=TIM_DIER_CC3DE;
    TIM4->CR1=TIM_CR1_CEN;
    __DSB();

}
static uint32_t live_fifo_go(void)
{
    TIM3->CR1=TIM_CR1_CEN;
    return DWT->CYCCNT;
}
static uint8_t live_fifo_finish(uint32_t start)
{
    /* No APB poll while RCLK is active. DWT timeout is a fixed deadline;
     * the counter gate must autonomously stop. Never continue after fault. */
    while((uint32_t)(DWT->CYCCNT-start)<(C3_LIVE_BYTES*C3_FIFO_DMA_LIVE_TICKS+144u))__NOP();
    live_wait(72u);
    unsigned cnt=TIM3->CNT;
    live_wait(72u);
    unsigned later=TIM3->CNT,pulses=TIM4->CNT,ndtr=DMA1_Channel2->CNDTR,flags=DMA1->ISR;
    unsigned pin=(GPIOB->IDR&GPIO_PIN_4)!=0u;
    uint8_t status=c3_live_check(C3_LIVE_BYTES,(flags&DMA_ISR_TEIF2)!=0u,pulses,cnt,later,
        live_plan.high_ticks,ndtr,(flags&DMA_ISR_TCIF2)!=0u,pin);
    TIM3->CR1=0u; TIM3->DIER=0u; TIM4->CR1=0u;
    DMA1_Channel2->CCR=0u; DMA1->IFCR=DMA_IFCR_CGIF2; __DSB();
    return status;
}
/* Block LCD DMA uses existing tested FSMC 8-bit write configuration.
 * Only one previous block is outstanding; CPU writes the other block.
 * DMA2 checks are always AFTER FIFO completion. */
static void live_lcd_start(unsigned b)
{
    DMA2_Channel1->CCR=0u; DMA2->IFCR=DMA_IFCR_CGIF1;
    DMA2_Channel1->CPAR=0x6C000400u;
    DMA2_Channel1->CMAR=(uint32_t)(uintptr_t)live_out[c3_live_slot(b)];
    DMA2_Channel1->CNDTR=C3_LIVE_BYTES;
    DMA2_Channel1->CCR=DMA_CCR_MEM2MEM|DMA_CCR_DIR|DMA_CCR_MINC|DMA_CCR_PL_0|DMA_CCR_EN;
}
static uint8_t live_lcd_finish(void)
{
    uint32_t t=DWT->CYCCNT; unsigned ok=0u;
    for(;;) {
        uint32_t f=DMA2->ISR;
        if(f&DMA_ISR_TEIF1)break;
        if(f&DMA_ISR_TCIF1){ok=DMA2_Channel1->CNDTR==0u;break;}
        if((uint32_t)(DWT->CYCCNT-t)>=720000u)break;
    }
    DMA2_Channel1->CCR=0u; DMA2->IFCR=DMA_IFCR_CGIF1; __DSB();
    return ok?0u:10u;
}
static void live_render_block(unsigned b,const c3_live_render_t *p,uint8_t ui_visible,
                              uint8_t card_red,uint8_t shutter_auto,uint8_t gain_auto)
{
    const uint16_t *src=live_raw[c3_live_slot(b)];
    uint8_t *dst=live_out[c3_live_slot(b)];
    for(unsigned r=0;r<C3_LIVE_ROWS;++r) {
        uint16_t y=(uint16_t)(b*C3_LIVE_ROWS+r);
        uint8_t *line=dst+r*C3_FD_BYTES;
        const uint16_t *s=src+r*C3_FD_BYTES;
        for(unsigned i=0;i<C3_FD_BYTES;i+=2u) {
            uint32_t v; uint16_t packed;
            __builtin_memcpy(&v,s+i,4u);
            packed=c3_live_unpack_pair(v);
            __builtin_memcpy(line+i,&packed,2u);
        }
        const uint8_t *mask=(ui_visible&&(y<C3_OSD_BAND_H||y>=C3_LCD_HEIGHT-C3_OSD_BAND_H))?c3_osd_row(y):0;
        c3_live_render_row(line,y,mask,p,&c3_focus_state,&c3_score_state,&peak_exp);
        if(card_red&&y>=C3_OSD_CARD_Y0&&y<C3_OSD_CARD_Y1)c3_osd_color_card_row(line,mask);
        if((shutter_auto||gain_auto)&&y>=C3_OSD_AUTO_Y0&&y<C3_OSD_AUTO_Y1)c3_osd_color_auto_row(line,mask,shutter_auto,gain_auto);
    }
}
static uint8_t live_draw(const c3_live_render_t *p,uint8_t ui_visible,uint8_t card_red,
                        uint8_t shutter_auto,uint8_t gain_auto)
{
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    uint32_t before=DWT->CYCCNT; __NOP();__NOP();__NOP();
    if(DWT->CYCCNT==before)return C3_FD_CLOCK;
    uint32_t start=before;
    RCC->AHBENR|=RCC_AHBENR_DMA2EN; (void)RCC->AHBENR; __DSB();
    if(!live_session_begin(&live_plan))return C3_FD_CLOCK;
    /* Prefill block0. During block b CPU reads raw[b], FIFO writes raw[b+1],
     * LCD reads out[b-1]. Parity keeps all three owners disjoint. */
    live_fifo_prepare(0u);
    uint8_t status=live_fifo_finish(live_fifo_go());
    c3_live_last_block=0u;
    for(unsigned b=0;!status&&b<C3_LIVE_BLOCKS;++b) {
        unsigned next=b+1u<C3_LIVE_BLOCKS,prev=b!=0u;
        uint32_t t=0u;
        if(next)live_fifo_prepare(b+1u);
        if(prev)live_lcd_start(b-1u);
        if(next)t=live_fifo_go();
        live_render_block(b,p,ui_visible,card_red,shutter_auto,gain_auto);
        if(next){status=live_fifo_finish(t);c3_live_last_block=b+1u;}
        if(prev){
            uint8_t lcd=live_lcd_finish();
            if(!status&&lcd){status=lcd;c3_live_last_block=b-1u;}
        }
    }
    if(!status){live_lcd_start(C3_LIVE_BLOCKS-1u);
        status=live_lcd_finish();
    }
    live_session_end();
    __DSB();for(volatile unsigned n=0;n<80u;++n)__NOP();
    c3_live_last_cycles=DWT->CYCCNT-start;
    if(!status)++c3_live_frames;
    return status;
}
static void live_error_hud(void)
{
    char msg[]="DMA->CPU E00";
    unsigned e=c3_live_last_error;
    msg[10]=(char)('0'+(e/10u)%10u);msg[11]=(char)('0'+e%10u);
    c3_lcd_rect(4,26,86,11,0u);
    c3_lcd_text(5,28,msg,0xffe0u,1);
}

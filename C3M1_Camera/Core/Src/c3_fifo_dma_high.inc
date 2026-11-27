/* v0.1.96 read-only high frequency sweep. */
#include "c3_fifo_dma_high_plan.h"
#include "c3_camera.h"
#include "c3_lcd.h"
#include "c3_board_pins.h"
#include "main.h"
#include <string.h>

#define REF_HEAD 32u
#define REF_TAIL 8u
#define TEST_REPEATS C3_FH_REPEATS
#define WHITE 0xffffu
#define CYAN 0x07ffu
#define GREEN 0x07e0u
#define RED 0xf800u
#define YELLOW 0xffe0u
/* 40 exact-comparison rows, CRC for all 240. A frozen frame is 153600 B;
 * there is only 64KiB SRAM, so no full-frame allocation. */
static uint8_t exact_ref[REF_HEAD+REF_TAIL][C3_FD_BYTES];
static uint32_t row_crc[C3_FD_ROWS];
static uint8_t row[C3_FD_BYTES] __attribute__((aligned(4)));
static uint16_t samples[C3_FD_BYTES] __attribute__((aligned(4)));
static uint8_t bad_sample[C3_FD_BYTES];
static uint32_t mapr_saved, crl_saved;
static uint8_t session;
/* Visible through SWD, also shown in the result detail page. */
typedef struct {
    uint32_t cycles, unpack_cycles, bad_rows, first_row, ref_crc, got_crc;
    uint32_t exact_bad, pulses, cnt, later, ndtr, flags, pin;
    uint16_t diff_count, first_byte, left_diff, right_diff;
    uint8_t expected, actual, diff_valid;
    uint8_t status, passes, phase, high_ticks, timed_out;
} fd_result_t;
static fd_result_t results[C3_FH_NMODE][C3_FH_NFREQ][C3_FH_NPHASE];
static uint32_t cpu_cycles, reference_bad, reference_unique, reference_changes;
static uint32_t baseline_crc, post_crc;
static uint8_t baseline_ok, post_ok, captured_ok;

static void elapsed_wait(uint32_t cycles)
{
    uint32_t t=DWT->CYCCNT;
    while((uint32_t)(DWT->CYCCNT-t)<cycles) __NOP();
}
static void dma_session_end(void)
{
    if(!session) return;
    TIM3->CR1=0u; TIM3->DIER=0u;
    TIM4->CR1=0u; TIM4->DIER=0u;
    DMA1_Channel2->CCR=0u;
    DMA1->IFCR=DMA_IFCR_CGIF2;
    __DSB();
    /* Restore GPIO with preloaded HIGH. Failed transfer may have lost a
     * read edge; never continue reading the same pointer after a failure. */
    GPIOB->BSRR=GPIO_PIN_4;
    GPIOB->CRL=(GPIOB->CRL & ~(0xfu<<16u)) | (crl_saved & (0xfu<<16u));
    TIM3->CCER=0u;
    TIM3->SMCR=0u; TIM4->SMCR=0u;
    AFIO->MAPR=(AFIO->MAPR & ~(AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG)) |
               (mapr_saved & AFIO_MAPR_TIM3_REMAP) | AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    __DSB();
    session=0u;
}
static uint8_t dma_session_begin(const c3_fd_plan_t *p)
{
    if(session || !p->ticks || HAL_RCC_GetHCLKFreq()!=72000000u ||
       HAL_RCC_GetPCLK1Freq()!=36000000u) return 0u;
    RCC->APB1ENR|=RCC_APB1ENR_TIM3EN|RCC_APB1ENR_TIM4EN;
    RCC->AHBENR|=RCC_AHBENR_DMA1EN;
    (void)RCC->APB1ENR; (void)RCC->AHBENR;
    __DSB();
    mapr_saved=AFIO->MAPR; crl_saved=GPIOB->CRL;
    session=1u;
    /* Dedicated bench image: neither timer nor DMA1_CH2 is used elsewhere. */
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
    TIM4->CCR1=C3_FD_BYTES; TIM4->EGR=TIM_EGR_UG; TIM4->SR=0u;
    /* F1 SWJ_CFG is not reliably readable. Always preserve SWD, disable JTAG. */
    AFIO->MAPR=(mapr_saved & ~(AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG)) |
               AFIO_MAPR_TIM3_REMAP_PARTIALREMAP|AFIO_MAPR_SWJ_CFG_JTAGDISABLE;
    GPIOB->BSRR=GPIO_PIN_4;
    /* PB4 only: AF push-pull 50MHz, PB3 WEN remains ordinary GPIO. */
    GPIOB->CRL=(crl_saved & ~(0xfu<<16u)) | (0xbu<<16u);
    __DSB();
    if(!(GPIOB->IDR&GPIO_PIN_4)) { dma_session_end(); return 0u; }
    return 1u;
}
static uint8_t dma_line(const c3_fd_plan_t *p,fd_result_t *r,unsigned quiet)
{
    uint32_t begin=DWT->CYCCNT, wall=HAL_GetTick();
    /* Break feedback BEFORE UG: a software update must never be counted
     * as a real RCLK edge. All requests disabled while programming. */
    TIM3->CR1=0u; TIM3->DIER=0u;
    TIM4->CR1=0u; TIM4->SMCR=0u;
    TIM3->SMCR=0u; TIM3->CR2=0u; TIM4->CR2=0u;
    TIM3->EGR=TIM_EGR_UG; TIM3->SR=0u;
    TIM4->EGR=TIM_EGR_UG; TIM4->SR=0u;
    DMA1_Channel2->CCR=0u; DMA1->IFCR=DMA_IFCR_CGIF2;
    DMA1_Channel2->CPAR=(uint32_t)(uintptr_t)&GPIOG->IDR;
    DMA1_Channel2->CMAR=(uint32_t)(uintptr_t)samples;
    DMA1_Channel2->CNDTR=C3_FD_BYTES;
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
    TIM3->CR1=TIM_CR1_CEN;
    uint8_t timeout=0u;
    /* Stop by hardware, not TC ISR. Timer count may be ahead of DMA TC;
     * give the final DMA request time to complete. Poll with hard deadline. */
    if(quiet) {
        /* No APB polling during this interval; interrupts remain enabled. */
        elapsed_wait(c3_fm_quiet_cycles(*p));
    } else while(TIM4->CNT<C3_FD_BYTES || DMA1_Channel2->CNDTR!=0u) {
        if(DMA1->ISR & DMA_ISR_TEIF2) break;
        if((uint32_t)(HAL_GetTick()-wall)>=5u ||
           (uint32_t)(DWT->CYCCNT-begin)>=360000u) {timeout=1u;break;}
    }
    /* Gating propagates across timers. Check stable CNT, exactly N cycles,
     * and no partial LOW period. Do not let software hide gate overrun. */
    elapsed_wait(72u);
    unsigned cnt=TIM3->CNT;
    elapsed_wait(72u);
    unsigned later=TIM3->CNT, pulses=TIM4->CNT;
    unsigned ndtr=DMA1_Channel2->CNDTR, flags=DMA1->ISR;
    unsigned pin=(GPIOB->IDR&GPIO_PIN_4)!=0u;
    uint8_t status=c3_fh_check((flags&DMA_ISR_TEIF2)!=0u,timeout,pulses,
        cnt,later,p->high_ticks,ndtr,(flags&DMA_ISR_TCIF2)!=0u,pin);
    TIM3->CR1=0u; TIM3->DIER=0u; TIM4->CR1=0u;
    DMA1_Channel2->CCR=0u; DMA1->IFCR=DMA_IFCR_CGIF2;
    __DSB();
    /* On a stop failure, callers discard this read and rewind before retry. */
    r->cycles+=(uint32_t)(DWT->CYCCNT-begin);
    if(status || r->passes==0u) {
        r->pulses=pulses; r->cnt=cnt; r->later=later;
        r->ndtr=ndtr; r->flags=flags; r->pin=pin; r->timed_out=timeout;
    }
    return status;
}
static uint32_t crc_row(void)
{
    CRC->CR=CRC_CR_RESET;
    for(unsigned x=0;x<C3_FD_BYTES;x+=4u) {
        /* Explicit packing: stable regardless of host alignment/endianness. */
        uint32_t v=(uint32_t)row[x]|((uint32_t)row[x+1u]<<8u)|
                   ((uint32_t)row[x+2u]<<16u)|((uint32_t)row[x+3u]<<24u);
        CRC->DR=v;
    }
    return CRC->DR;
}
static int exact_slot(unsigned y)
{
    if(y<REF_HEAD) return (int)y;
    if(y>=C3_FD_ROWS-REF_TAIL) return (int)(REF_HEAD+y-(C3_FD_ROWS-REF_TAIL));
    return -1;
}
static unsigned exact_diff(unsigned y)
{
    int slot=exact_slot(y); unsigned n=0u;
    if(slot>=0) for(unsigned x=0;x<C3_FD_BYTES;x++) n+=row[x]!=exact_ref[slot][x];
    return n;
}
static uint32_t cpu_pass(uint8_t save, uint32_t *bad)
{
    uint32_t cycles=0u, aggregate=0x811c9dc5u;
    uint32_t changes=0u; uint8_t seen[256]={0};
    *bad=0u;
    if(!c3_camera_raw_rewind()) {*bad=C3_FD_ROWS;return 0u;}
    for(unsigned y=0;y<C3_FD_ROWS;y++) {
        uint32_t t=DWT->CYCCNT;
        for(unsigned x=0;x<C3_FD_BYTES;x++) row[x]=c3_camera_fifo_read_fast();
        cycles+=DWT->CYCCNT-t;
        uint32_t c=crc_row(); aggregate=(aggregate^c)*16777619u;
        if(save) {
            row_crc[y]=c;
            int slot=exact_slot(y);
            if(slot>=0) memcpy(exact_ref[slot],row,C3_FD_BYTES);
            for(unsigned x=0;x<C3_FD_BYTES;x++) {
                seen[row[x]]=1u;
                if(x) changes+=row[x]!=row[x-1u];
            }
        } else if(c!=row_crc[y] || exact_diff(y)) ++*bad;
    }
    c3_camera_raw_read_end();
    if(save) {
        cpu_cycles=cycles; reference_changes=changes; reference_unique=0;
        for(unsigned x=0;x<256u;x++) reference_unique+=seen[x];
    }
    return aggregate;
}
/* Compare first failing DMA row to a CPU reread. Check the latter
 * against the initial CRC before trusting the byte-level diagnostic. */
static void diagnose_row(fd_result_t *r)
{
    if(r->first_row>=C3_FD_ROWS) return;
    if(!c3_camera_raw_rewind()) {r->status=C3_FD_REFERENCE;return;}
    for(unsigned y=0;y<=r->first_row;y++)
        for(unsigned x=0;x<C3_FD_BYTES;x++) row[x]=c3_camera_fifo_read_fast();
    c3_camera_raw_read_end();
    if(crc_row()!=row_crc[r->first_row] || exact_diff(r->first_row)) {
        r->status=C3_FD_REFERENCE; return;
    }
    c3_fm_diff_t d=c3_fm_diff(row,bad_sample);
    r->first_byte=d.first;r->diff_count=d.count;r->left_diff=d.left;r->right_diff=d.right;
    r->expected=d.expected;r->actual=d.actual;
    r->diff_valid=1u;
}
static void trial(unsigned f,unsigned p,unsigned quiet,fd_result_t *r)
{
    c3_fd_plan_t plan=c3_fh_plan(f,p);
    memset(r,0,sizeof(*r)); r->first_row=C3_FD_ROWS;r->first_byte=C3_FD_BYTES;
    r->phase=plan.sample_tick; r->high_ticks=plan.high_ticks;
    for(unsigned rep=0;rep<TEST_REPEATS;rep++) {
        uint32_t prior=r->cycles,prior_unpack=r->unpack_cycles;
        if(!c3_camera_raw_rewind()) { r->status=C3_FD_REFERENCE; break; }
        if(!dma_session_begin(&plan)) {
            r->status=C3_FD_CLOCK; c3_camera_raw_read_end();break;
        }
        for(unsigned y=0;y<C3_FD_ROWS;y++) {
            uint8_t st=dma_line(&plan,r,quiet);
            if(st) {
                r->status=st;
                if(r->first_row==C3_FD_ROWS) r->first_row=y;
                break;
            }
            uint32_t unpack_start=DWT->CYCCNT;
            for(unsigned x=0;x<C3_FD_BYTES;x++) row[x]=c3_fd_unpack(samples[x]);
            r->unpack_cycles+=DWT->CYCCNT-unpack_start;
            unsigned exact=exact_diff(y); uint32_t c=crc_row();
            r->exact_bad+=exact;
            if(c!=row_crc[y] || exact) {
                ++r->bad_rows;
                if(r->first_row==C3_FD_ROWS) {
                    r->first_row=y;r->ref_crc=row_crc[y];r->got_crc=c;
                    memcpy(bad_sample,row,C3_FD_BYTES);
                }
            }
        }
        dma_session_end(); c3_camera_raw_read_end();
        if(!r->status && r->bad_rows) r->status=C3_FD_DATA;
        if(r->status) {
            r->cycles=prior;r->unpack_cycles=prior_unpack;
            if(r->bad_rows) diagnose_row(r);
            break;
        }
        ++r->passes;
    }
    if(r->passes) { r->cycles/=r->passes; r->unpack_cycles/=r->passes; }
}
static void dec(char *s,unsigned n,unsigned digits)
{
    for(unsigned i=0;i<digits;i++) {s[digits-1u-i]=(char)('0'+n%10u);n/=10u;}
}
static void hex(char *s,uint32_t n,unsigned digits)
{
    static const char h[]="0123456789ABCDEF";
    for(unsigned i=0;i<digits;i++) {s[digits-1u-i]=h[n&15u];n>>=4u;}
}
static void tenths(char *s,uint32_t cycles)
{
    /* 72 MHz = 7200 cycles per 0.1ms. */
    unsigned v=(unsigned)((cycles+3600u)/7200u);if(v>9999u)v=9999u;
    dec(s,v/10u,3u);s[3]='.';s[4]=(char)('0'+v%10u);
}
static void grid_base(unsigned mode)
{
    c3_lcd_fill(0u);
    c3_lcd_text(4,4,mode?"DMA196 QUIET":"DMA196 POLL",CYAN,2);
    char ref[]="CPU 000.0 MS UNIQUE 000";
    tenths(ref+4,cpu_cycles);dec(ref+20,reference_unique,3);
    c3_lcd_text(4,25,ref,WHITE,1);
    c3_lcd_text(4,38,"MHZ      T1         T2         T3         T4",WHITE,1);
    for(unsigned f=0;f<C3_FH_NFREQ;f++) {
        char n[]="00.00";unsigned v=c3_fm_freq100(c3_fh_plan(f,0));
        dec(n,v/100u,2);dec(n+3,v%100u,2);
        c3_lcd_text(4,52+(int)f*18,n,CYAN,1);
    }
}
static void cell(unsigned f,unsigned p,const fd_result_t *r)
{
    unsigned x=54u+p*66u,y=50u+f*18u;
    c3_lcd_rect((int)x,(int)y,64,18,0u);
    if(r->status==0 && r->passes==TEST_REPEATS) {
        char s[]="P000.0";tenths(s+1,r->cycles);
        c3_lcd_text((int)x,(int)y+3,s,GREEN,1);
    } else {
        char s[]="E0 R000";dec(s+1,r->status,1);dec(s+4,r->first_row,3);
        char n[]="PASS 00";dec(n+5,r->passes,2);
        c3_lcd_text((int)x,(int)y,s,RED,1);
        c3_lcd_text((int)x,(int)y+9,n,WHITE,1);
    }
}
static void summary(unsigned mode)
{
    grid_base(mode);
    if(baseline_ok) for(unsigned f=0;f<C3_FH_NFREQ;f++)
        for(unsigned p=0;p<C3_FH_NPHASE;p++)cell(f,p,&results[mode][f][p]);
    char st[]="CPU REPEAT BAD ROWS 000";dec(st+20,reference_bad,3);
    const char *baseline_message=baseline_ok?"CPU REPEAT OK":!captured_ok?"CAPTURE FAILED":reference_bad?st:"LOW DETAIL - REAIM";
    c3_lcd_text(4,186,baseline_message,baseline_ok?GREEN:RED,1);
    c3_lcd_text(4,198,post_ok?"FINAL CPU OK":baseline_ok?"INVALID RESULTS / FINAL CPU FAIL":"SWEEP NOT RUN",post_ok?GREEN:RED,1);
    c3_lcd_text(4,210,"P=32 PASS MS  E=ERR R=ROW",WHITE,1);
    c3_lcd_text(4,222,"SHOT=RETEST ISO=DETAIL 4X=MODE",YELLOW,1);
}
static void detail(unsigned mode,unsigned n)
{
    unsigned f=n/C3_FH_NPHASE,p=n%C3_FH_NPHASE;
    fd_result_t *r=&results[mode][f][p]; c3_fd_plan_t t=c3_fh_plan(f,p);
    unsigned v=c3_fm_freq100(t);
    char a[]="00.00 MHZ TICK 0/00";dec(a+0,v/100u,2);dec(a+3,v%100u,2);dec(a+15,t.sample_tick,1);dec(a+17,t.ticks,2);
    char b[]="STATUS 0 PASS 00 ROW 000 TO 0";dec(b+7,r->status,1);dec(b+14,r->passes,2);dec(b+21,r->first_row,3);dec(b+28,r->timed_out,1);
    char c[]="PULSES 0000 CNT 00/00";dec(c+7,r->pulses,4);dec(c+16,r->cnt,2);dec(c+19,r->later,2);
    char d[]="NDTR 0000 FLAGS 0000 PIN 0";dec(d+5,r->ndtr,4);hex(d+16,r->flags,4);dec(d+25,r->pin,1);
    char e[]="BAD ROWS 000 FIRST BYTE 000";dec(e+9,r->bad_rows,3);dec(e+24,r->first_byte,3);
    char g[]="REF 00000000 GOT 00000000";hex(g+4,r->ref_crc,8);hex(g+17,r->got_crc,8);
    char h[]="DIFF 000 CPU 00 DMA 00";dec(h+5,r->diff_count,3);hex(h+13,r->expected,2);hex(h+20,r->actual,2);
    char sh[]="SHIFT -1 000 +1 000";dec(sh+9,r->left_diff,3);dec(sh+16,r->right_diff,3);
    char tm[]="READ 000.0 UNPACK 000.0 MS";tenths(tm+5,r->cycles);tenths(tm+18,r->unpack_cycles);
    c3_lcd_fill(0u); c3_lcd_text(4,4,mode?"QUIET DETAIL":"POLL DETAIL",CYAN,2);
    const char *lines[]={a,b,c,d,e,g,r->diff_valid?h:"NO VERIFIED BYTE DIFF",r->diff_valid?sh:"",tm};
    for(unsigned i=0;i<9u;i++)c3_lcd_text(4,30+(int)i*19,lines[i],WHITE,1);
    c3_lcd_text(4,207,"R240 / BYTE640 = NO ERROR",WHITE,1);
    c3_lcd_text(4,222,"ISO=NEXT 4X=GRID SHOT=RETEST",YELLOW,1);
}
static uint8_t wait_key(void)
{
    /* Poll stable press/release directly (probe only; no UI side effects). */
    for(;;) {
        uint16_t k=(uint16_t)(~GPIOF->IDR)&C3_BTN_MASK;
        if(k) { HAL_Delay(25u); if((((uint16_t)(~GPIOF->IDR))&k)==k) {
            while(((uint16_t)(~GPIOF->IDR))&C3_BTN_MASK) HAL_Delay(10u);
            HAL_Delay(25u);
            if(k&C3_BTN_SHUTTER) return 1u;
            if(k&C3_BTN_ISO) return 2u;
            if(k&C3_BTN_4X) return 3u;
        }}
        HAL_Delay(10u);
    }
}
void c3_fifo_dma_probe_run(uint8_t camera_ok)
{
    c3_camera_preview_stop();
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT=0u;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    RCC->AHBENR|=RCC_AHBENR_CRCEN;(void)RCC->AHBENR;
    __DSB();
    uint32_t d0=DWT->CYCCNT; HAL_Delay(1u);
    if(DWT->CYCCNT==d0 || HAL_RCC_GetHCLKFreq()!=72000000u || HAL_RCC_GetPCLK1Freq()!=36000000u) {
        c3_lcd_message("DWT / CLOCK ERR");for(;;)HAL_Delay(100u);
    }
    if(!camera_ok) {c3_lcd_message("CAM INIT ERR");for(;;)HAL_Delay(100u);}
    c3_lcd_fill(0u);c3_lcd_text(4,8,"DMA196 1/100 AUTO ISO",CYAN,2);
    c3_lcd_text(4,42,"AIM AT BRIGHT TEXTURED SCENE",WHITE,1);
    c3_lcd_text(4,60,"SHOT STARTS HIGH SWEEP",YELLOW,2);
    c3_lcd_text(4,88,"NO SD / NO SETTINGS WRITE",WHITE,1);
    while(wait_key()!=1u){}
    for(;;) {
        baseline_ok=post_ok=captured_ok=0u;reference_bad=0u;cpu_cycles=0u;reference_unique=0u;
        memset(results,0,sizeof(results));
        c3_lcd_message("CAPTURE REFERENCE");
        /* Main has applied validated 1/100 S, 1X preview. Do not alter PCLK,
         * sensor configuration, or WCK during frozen read experiments. */
        HAL_Delay(200u);
        if(c3_camera_bmp_fast_init() && c3_camera_capture(1600u) && c3_camera_raw_hold()) {
            captured_ok=1u;
            baseline_crc=cpu_pass(1u,&reference_bad);
            uint32_t bad=0u, check=cpu_pass(0u,&bad);
            reference_bad+=bad;
            baseline_ok=(reference_bad==0u && check==baseline_crc && reference_unique>=16u && reference_changes>1000u);
            if(baseline_ok) {
                /* Same frozen reference for both modes. No LCD access
                 * during a line transfer. */
                for(int mode=1;mode>=0;mode--) {
                    grid_base((unsigned)mode);
                    for(unsigned f=0;f<C3_FH_NFREQ;f++)for(unsigned p=0;p<C3_FH_NPHASE;p++) {
                        trial(f,p,(unsigned)mode,&results[mode][f][p]);cell(f,p,&results[mode][f][p]);
                    }
                }
                post_crc=cpu_pass(0u,&bad);
                post_ok=(bad==0u && post_crc==baseline_crc);
            }
            c3_camera_raw_discard();
        } else { reference_bad=C3_FD_ROWS; c3_camera_raw_discard(); }
        unsigned mode=1u,page=0u,in_detail=0u;summary(mode);
        for(;;) {
            unsigned key=wait_key();if(key==1u)break;
            if(key==3u) {if(!in_detail)mode^=1u;summary(mode);page=0u;in_detail=0u;}
            if(key==2u && baseline_ok) {detail(mode,page);page=(page+1u)%(C3_FH_NFREQ*C3_FH_NPHASE);in_detail=1u;}
        }
    }
}

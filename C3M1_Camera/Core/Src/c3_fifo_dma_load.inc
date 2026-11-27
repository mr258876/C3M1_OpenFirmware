/* v0.1.97 frozen-frame concurrent load sweep. */
#include "c3_fifo_dma_load_plan.h"
#include "c3_peak_experiment.h"
#include "c3_camera.h"
#include "c3_lcd.h"
#include "c3_board_pins.h"
#include "main.h"
#include <string.h>

#define REF_HEAD 32u
#define REF_TAIL 8u
#define TEST_REPEATS C3_FL_REPEATS
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
static uint16_t samples[2][C3_FD_BYTES] __attribute__((aligned(4)));
static uint8_t out[2][C3_FD_BYTES] __attribute__((aligned(4)));
static c3_focus_t focus;
static volatile uint32_t cpu_sink;
#define LCD_DATA_ADDR 0x6C000400u
/* Actual LCD DMA2 CH1, same MEM2MEM direction, width and priority as mainline.
 * No fallback: a DMA fault invalidates the combination. No polling until
 * FIFO has finished. LCD accesses are restricted to this diagnostic. */
static void lcd_start(unsigned y)
{
    DMA2_Channel1->CCR=0u; DMA2->IFCR=DMA_IFCR_CGIF1;
    DMA2_Channel1->CPAR=LCD_DATA_ADDR;
    DMA2_Channel1->CMAR=(uint32_t)(uintptr_t)out[c3_fl_slot(y)];
    DMA2_Channel1->CNDTR=C3_FD_BYTES;
    DMA2_Channel1->CCR=DMA_CCR_MEM2MEM|DMA_CCR_DIR|DMA_CCR_MINC|DMA_CCR_PL_0|DMA_CCR_EN;
}
static uint8_t lcd_done(void)
{
    uint32_t t=DWT->CYCCNT;
    uint8_t ok=0u;
    for(;;) {
        uint32_t f=DMA2->ISR;
        if(f&DMA_ISR_TEIF1)break;
        if(f&DMA_ISR_TCIF1){ok=DMA2_Channel1->CNDTR==0u;break;}
        if((uint32_t)(DWT->CYCCNT-t)>=720000u)break;
    }
    DMA2_Channel1->CCR=0u; DMA2->IFCR=DMA_IFCR_CGIF1; __DSB();
    return ok;
}
/* Actual dense green luma/gradient kernel, with fixed threshold for repeatable
 * stress. Raw verification is separate and never sees painted pixels. */
static void prepare(unsigned y,unsigned peak)
{
    const uint16_t *src=samples[c3_fl_slot(y)];
    uint8_t *dst=out[c3_fl_slot(y)];
    for(unsigned x=0;x<C3_FD_BYTES;++x)dst[x]=c3_fd_unpack(src[x]);
    if(peak)c3_peak_fast_pixels(dst,(uint16_t)y,C3_PEAK_AREA_FULL,&focus,16u,0xf800u,0u,0);
    cpu_sink+=dst[(y*13u)%C3_FD_BYTES];
}
static uint8_t bad_sample[C3_FD_BYTES];
static uint32_t mapr_saved, crl_saved;
static uint8_t session;
/* Visible through SWD, also shown in the result detail page. */
typedef struct {
    uint32_t cycles, unpack_cycles, bad_rows, first_row, ref_crc, got_crc;
    uint32_t cpu_work, overlap, active_lcd, full_cycles;
    uint32_t exact_bad, pulses, cnt, later, ndtr, flags, pin;
    uint16_t diff_count, first_byte, left_diff, right_diff;
    uint8_t expected, actual, diff_valid;
    uint8_t status, passes, phase, high_ticks, timed_out;
} fd_result_t;
static fd_result_t results[C3_FL_NMODE][C3_FL_NFREQ][C3_FL_NPHASE];
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
static uint8_t dma_line(const c3_fd_plan_t *p,fd_result_t *r,unsigned mode,unsigned y)
{
    uint32_t begin=DWT->CYCCNT;
    /* Break feedback BEFORE UG: a software update must never be counted
     * as a real RCLK edge. All requests disabled while programming. */
    TIM3->CR1=0u; TIM3->DIER=0u;
    TIM4->CR1=0u; TIM4->SMCR=0u;
    TIM3->SMCR=0u; TIM3->CR2=0u; TIM4->CR2=0u;
    TIM3->EGR=TIM_EGR_UG; TIM3->SR=0u;
    TIM4->EGR=TIM_EGR_UG; TIM4->SR=0u;
    DMA1_Channel2->CCR=0u; DMA1->IFCR=DMA_IFCR_CGIF2;
    DMA1_Channel2->CPAR=(uint32_t)(uintptr_t)&GPIOG->IDR;
    DMA1_Channel2->CMAR=(uint32_t)(uintptr_t)samples[c3_fl_slot(y)];
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
    unsigned lcd=(mode&C3_FL_LCD) && y>=2u;
    unsigned work=(mode&C3_FL_CPU) && y>=1u;
    unsigned lcd_remaining=0u;
    if(lcd) {
        lcd_start(y-2u);
        /* One pre-start snapshot, not polling; require at least 32 bytes
         * outstanding to leave a margin for TIM3 start propagation. */
        lcd_remaining=DMA2_Channel1->CNDTR;
        ++r->active_lcd;
        if(lcd_remaining>=32u)++r->overlap;
    }
    TIM3->CR1=TIM_CR1_CEN;
    uint32_t t=DWT->CYCCNT;
    if(work) {
        prepare(y-1u,1u);
        r->cpu_work+=DWT->CYCCNT-t;
    }
    /* CPU work overlaps FIFO. No peripheral polls during the active interval.
     * Do not add a second full wait after CPU work. */
    while((uint32_t)(DWT->CYCCNT-t)<c3_fm_quiet_cycles(*p))__NOP();
    uint8_t timeout=0u;
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
    /* FIFO stopped before checking LCD. Never launch a new LCD command
     * while DMA2 is still active. */
    if(lcd && !lcd_done() && !status)status=C3_FL_LCD_ERROR;
    if(lcd && lcd_remaining<32u && !status)status=C3_FL_NO_OVERLAP;
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
static void trial(unsigned f,unsigned p,unsigned mode,fd_result_t *r)
{
    c3_fd_plan_t plan=c3_fl_plan(f,p);
    memset(r,0,sizeof(*r)); r->first_row=C3_FD_ROWS;r->first_byte=C3_FD_BYTES;
    r->phase=plan.sample_tick; r->high_ticks=plan.high_ticks;
    for(unsigned rep=0;rep<TEST_REPEATS;rep++) {
        uint32_t prior=r->cycles,prior_unpack=r->unpack_cycles,prior_cpu=r->cpu_work;
        uint32_t prior_overlap=r->overlap,prior_lcd=r->active_lcd;
        uint32_t start=DWT->CYCCNT;
        if(!c3_camera_raw_rewind()) { r->status=C3_FD_REFERENCE; break; }
        if(!dma_session_begin(&plan)) {
            r->status=C3_FD_CLOCK; c3_camera_raw_read_end();break;
        }
        memset(&focus,0,sizeof(focus));
        if(mode&C3_FL_LCD)c3_lcd_window(0,0,319,239);
        for(unsigned y=0;y<C3_FD_ROWS;y++) {
            uint8_t st=dma_line(&plan,r,mode,y);
            if(st) {r->status=st; r->first_row=y; break;}
            uint32_t unpack_start=DWT->CYCCNT;
            for(unsigned x=0;x<C3_FD_BYTES;x++)row[x]=c3_fd_unpack(samples[c3_fl_slot(y)][x]);
            r->unpack_cycles+=DWT->CYCCNT-unpack_start;
            unsigned exact=exact_diff(y); uint32_t c=crc_row();
            r->exact_bad+=exact;
            if(c!=row_crc[y] || exact) {
                ++r->bad_rows; r->first_row=y;r->ref_crc=row_crc[y];r->got_crc=c;
                memcpy(bad_sample,row,C3_FD_BYTES);
                r->status=C3_FD_DATA; break;
            }
            /* LCD-only: prepare raw pixels outside active FIFO interval.
             * BOTH: previous line is prepared during next FIFO transfer. */
            if((mode&C3_FL_LCD) && !(mode&C3_FL_CPU))prepare(y,0u);
        }
        dma_session_end(); c3_camera_raw_read_end();
        if(!r->status && (mode&C3_FL_LCD)) {
            /* Drain the last two displayed rows outside FIFO. In BOTH,
             * row238 was already prepared by iteration239. */
            if(mode&C3_FL_CPU)prepare(239u,1u);
            lcd_start(238u); if(!lcd_done())r->status=C3_FL_LCD_ERROR;
            if(!r->status){lcd_start(239u);if(!lcd_done())r->status=C3_FL_LCD_ERROR;}
            __DSB(); for(volatile unsigned n=0;n<80u;++n)__NOP();
        }
        if(r->status) {
            r->cycles=prior;r->unpack_cycles=prior_unpack;r->cpu_work=prior_cpu;
            r->overlap=prior_overlap;r->active_lcd=prior_lcd;
            if(r->bad_rows)diagnose_row(r);
            break;
        }
        r->full_cycles+=DWT->CYCCNT-start;
        ++r->passes;
    }
    if(r->passes) {
        r->cycles/=r->passes; r->unpack_cycles/=r->passes; r->cpu_work/=r->passes;
        r->full_cycles/=r->passes;
    }
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
static const char *mode_title(unsigned mode)
{
    static const char *t[]={"DMA197 QUIET","DMA197 CPU","DMA197 LCD","DMA197 BOTH"};
    return t[mode&3u];
}
static void grid_base(unsigned mode)
{
    c3_lcd_fill(0u);
    c3_lcd_text(4,4,mode_title(mode),CYAN,2);
    char ref[]="CPU 000.0 MS UNIQUE 000";
    tenths(ref+4,cpu_cycles);dec(ref+20,reference_unique,3);
    c3_lcd_text(4,25,ref,WHITE,1);
    c3_lcd_text(4,38,"MHZ          T1               T2",WHITE,1);
    for(unsigned f=0;f<C3_FL_NFREQ;f++) {
        char n[]="00.00";unsigned v=c3_fm_freq100(c3_fl_plan(f,0));
        dec(n,v/100u,2);dec(n+3,v%100u,2);
        c3_lcd_text(4,52+(int)f*18,n,CYAN,1);
    }
}
static void cell(unsigned f,unsigned p,const fd_result_t *r)
{
    unsigned x=76u+p*120u,y=50u+f*18u;
    c3_lcd_rect((int)x,(int)y,116,18,0u);
    if(r->status==0 && r->passes==TEST_REPEATS) {
        char s[]="P000.0";tenths(s+1,r->cycles);
        c3_lcd_text((int)x,(int)y+3,s,GREEN,1);
    } else {
        char s[]="E00 R000";dec(s+1,r->status,2);dec(s+5,r->first_row,3);
        char n[]="PASS 00";dec(n+5,r->passes,2);
        c3_lcd_text((int)x,(int)y,s,RED,1);
        c3_lcd_text((int)x,(int)y+9,n,WHITE,1);
    }
}
static void summary(unsigned mode)
{
    grid_base(mode);
    if(baseline_ok) for(unsigned f=0;f<C3_FL_NFREQ;f++)
        for(unsigned p=0;p<C3_FL_NPHASE;p++)cell(f,p,&results[mode][f][p]);
    char st[]="CPU REPEAT BAD ROWS 000";dec(st+20,reference_bad,3);
    const char *baseline_message=baseline_ok?"CPU REPEAT OK":!captured_ok?"CAPTURE FAILED":reference_bad?st:"LOW DETAIL - REAIM";
    c3_lcd_text(4,186,baseline_message,baseline_ok?GREEN:RED,1);
    c3_lcd_text(4,198,post_ok?"FINAL CPU OK":baseline_ok?"INVALID RESULTS / FINAL CPU FAIL":"SWEEP NOT RUN",post_ok?GREEN:RED,1);
    c3_lcd_text(4,210,"P=32 PASS MS E10=LCD E11=OVERLAP",WHITE,1);
    c3_lcd_text(4,222,"SHOT=RETEST ISO=DETAIL 4X=MODE",YELLOW,1);
}
static void detail(unsigned mode,unsigned n)
{
    unsigned f=n/C3_FL_NPHASE,p=n%C3_FL_NPHASE;
    fd_result_t *r=&results[mode][f][p]; c3_fd_plan_t t=c3_fl_plan(f,p);
    unsigned v=c3_fm_freq100(t);
    char a[]="00.00 MHZ TICK 0/00";dec(a+0,v/100u,2);dec(a+3,v%100u,2);dec(a+15,t.sample_tick,1);dec(a+17,t.ticks,2);
    char b[]="STATUS 00 PASS 00 ROW 000";dec(b+7,r->status,2);dec(b+15,r->passes,2);dec(b+22,r->first_row,3);
    char c[]="PULSES 0000 CNT 00/00";dec(c+7,r->pulses,4);dec(c+16,r->cnt,2);dec(c+19,r->later,2);
    char d[]="NDTR 0000 FLAGS 0000 PIN 0";dec(d+5,r->ndtr,4);hex(d+16,r->flags,4);dec(d+25,r->pin,1);
    char e[]="BAD ROWS 000 FIRST BYTE 000";dec(e+9,r->bad_rows,3);dec(e+24,r->first_byte,3);
    char g[]="REF 00000000 GOT 00000000";hex(g+4,r->ref_crc,8);hex(g+17,r->got_crc,8);
    char h[]="DIFF 000 CPU 00 DMA 00";dec(h+5,r->diff_count,3);hex(h+13,r->expected,2);hex(h+20,r->actual,2);
    char sh[]="SHIFT -1 000 +1 000";dec(sh+9,r->left_diff,3);dec(sh+16,r->right_diff,3);
    char tm[]="STAGE 000.0 CPU 000.0 MS";tenths(tm+6,r->cycles);tenths(tm+16,r->cpu_work);
    char load[]="LCD OVERLAP 0000/0000";dec(load+12,r->overlap,4);dec(load+17,r->active_lcd,4);
    char total[]="TOTAL 000.0 UNPACK 000.0";tenths(total+6,r->full_cycles);tenths(total+19,r->unpack_cycles);
    c3_lcd_fill(0u); c3_lcd_text(4,4,mode_title(mode),CYAN,2);
    const char *lines[]={a,b,c,d,e,g,r->diff_valid?h:"NO VERIFIED BYTE DIFF",r->diff_valid?sh:"",tm,load,total};
    for(unsigned i=0;i<11u;i++)c3_lcd_text(4,26+(int)i*16,lines[i],WHITE,1);
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
    RCC->AHBENR|=RCC_AHBENR_CRCEN|RCC_AHBENR_DMA2EN;(void)RCC->AHBENR;
    __DSB();
    uint32_t d0=DWT->CYCCNT; HAL_Delay(1u);
    if(DWT->CYCCNT==d0 || HAL_RCC_GetHCLKFreq()!=72000000u || HAL_RCC_GetPCLK1Freq()!=36000000u) {
        c3_lcd_message("DWT / CLOCK ERR");for(;;)HAL_Delay(100u);
    }
    if(!camera_ok) {c3_lcd_message("CAM INIT ERR");for(;;)HAL_Delay(100u);}
    c3_lcd_fill(0u);c3_lcd_text(4,8,"DMA197 1/100 AUTO ISO",CYAN,2);
    c3_lcd_text(4,42,"AIM AT BRIGHT TEXTURED SCENE",WHITE,1);
    c3_lcd_text(4,60,"SHOT STARTS LOAD SWEEP",YELLOW,2);
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
                /* Identical frozen frame, four load modes. LCD modes display
                 * the tested frame; redraw a progress page between trials. */
                for(unsigned mode=0;mode<C3_FL_NMODE;mode++) {
                    for(unsigned f=0;f<C3_FL_NFREQ;f++)for(unsigned p=0;p<C3_FL_NPHASE;p++) {
                        grid_base(mode);
                        char progress[]="TEST 00/14";dec(progress+5,f*C3_FL_NPHASE+p+1u,2);
                        c3_lcd_text(4,198,progress,YELLOW,1);
                        trial(f,p,mode,&results[mode][f][p]);
                    }
                }
                post_crc=cpu_pass(0u,&bad);
                post_ok=(bad==0u && post_crc==baseline_crc);
            }
            c3_camera_raw_discard();
        } else { reference_bad=C3_FD_ROWS; c3_camera_raw_discard(); }
        unsigned mode=C3_FL_BOTH,page=0u,in_detail=0u;summary(mode);
        for(;;) {
            unsigned key=wait_key();if(key==1u)break;
            if(key==3u) {if(!in_detail)mode=(mode+1u)%C3_FL_NMODE;summary(mode);page=0u;in_detail=0u;}
            if(key==2u && baseline_ok) {detail(mode,page);page=(page+1u)%(C3_FL_NFREQ*C3_FL_NPHASE);in_detail=1u;}
        }
    }
}

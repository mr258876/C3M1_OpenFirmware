/* v0.1.45: standalone OV7725 register-read matrix.
 * This file is ONLY linked by C3_SCCB_SCAN_DIAG=1 + read-only build.
 * It never captures a frame, writes SD, or writes MCU settings Flash.
 * Sensor SCCB writes below are temporary: the page switch resets the sensor.
 */
#include <stdint.h>
#include "main.h"
#include "c3_lcd.h"
#include "c3_board_pins.h"
#include "c3_sccb_diag.h"
#include "../../Drivers/BSP/OV7725/ov7725.h"
#include "../../Drivers/BSP/OV7725/sccb.h"

#if !C3_SCCB_SCAN_DIAG || !C3_PREVIEW_PROBE || C3_FIFO_PROBE
#error "Only link c3_sccb_diag.c into a read-only SCCB diagnostic"
#endif

typedef struct {
    uint8_t reg, expected, mask;
    uint8_t l_last, r_last;
    uint8_t l_good, r_good, l_ff, r_ff;
} sccb_scan_item_t;

/* Read-only fixed identification, selected static R/W setup registers.
 * Keep COM7/DSP4 as RAW-vs-RGB expected; never check volatile AEC/GAIN. */
static sccb_scan_item_t scan[] = {
    {0x0Au, 0x77u, 0xFFu, 0,0,0,0,0,0}, /* PID */
    {0x0Bu, 0x21u, 0xFFu, 0,0,0,0,0,0}, /* VER */
    {0x1Cu, 0x7Fu, 0xFFu, 0,0,0,0,0,0}, /* MIDH */
    {0x1Du, 0xA2u, 0xFFu, 0,0,0,0,0,0}, /* MIDL */
    {0x12u, 0x13u, 0xFFu, 0,0,0,0,0,0}, /* COM7 */
    {0x67u, 0x02u, 0x03u, 0,0,0,0,0,0}, /* RAW8/RGB mode */
    {0x94u, 0xB0u, 0xFFu, 0,0,0,0,0,0}, /* MTX1 */
    {0x95u, 0x9Du, 0xFFu, 0,0,0,0,0,0}, /* MTX2 */
};

/* v0.1.47: scan actual register values, not only expected-value matches.
 * Adjacent readable, writable and reserved subaddresses help distinguish
 * failure specific to MTX1/2 from an entire 0x9x read-address problem.
 * Never write or "test-pattern" these registers during scanning. */
typedef struct {
    uint8_t reg;
    uint8_t leg_last, alt_last;
    uint8_t leg_ff, alt_ff;
} sccb_neighbor_t;
static sccb_neighbor_t neighbor[] = {
    {0x8Eu,0,0,0,0}, {0x8Fu,0,0,0,0}, {0x90u,0,0,0,0},
    {0x91u,0,0,0,0}, {0x92u,0,0,0,0}, {0x93u,0,0,0,0},
    {0x94u,0,0,0,0}, {0x95u,0,0,0,0}, {0x96u,0,0,0,0},
    {0x97u,0,0,0,0}, {0x98u,0,0,0,0}, {0x99u,0,0,0,0},
    {0x9Au,0,0,0,0}, {0x9Bu,0,0,0,0}, {0x9Cu,0,0,0,0}
};
static void scan_neighbors(void)
{
    for (unsigned r=0; r<sizeof(neighbor)/sizeof(neighbor[0]); ++r) {
        sccb_neighbor_t *n=&neighbor[r];
        n->leg_ff=n->alt_ff=0u;
        for (unsigned j=0; j<5u; ++j) {
            n->leg_last=ov7725_read_reg(n->reg);
            n->alt_last=ov7725_read_reg_restart(n->reg);
            if (n->leg_last==0xFFu) ++n->leg_ff;
            if (n->alt_last==0xFFu) ++n->alt_ff;
        }
    }
}


/* v0.1.48: investigate a DATA-bit-7 correlation, not an address-range
 * correlation. The failing observed bytes A2/B0/9D/91 all have MSB=1,
 * while the adjacent successful 13/16/7B values have MSB=0.
 * Read the entire gamma-table transition across 0x80 and fixed ID bytes.
 * These are READS ONLY, and the last values remain visible with FF counts.
 */
typedef struct {
    uint8_t reg, expected;
    uint8_t leg_last, alt_last;
    uint8_t leg_ff, alt_ff;
} c3_msb_scan_t;
static c3_msb_scan_t msb_scan[] = {
    {0x84u,0x7Bu,0,0,0,0},  /* GAM7: below 0x80 */
    {0x85u,0x86u,0,0,0,0},  /* GAM8: above 0x80 */
    {0x86u,0x8Eu,0,0,0,0},
    {0x87u,0x97u,0,0,0,0},
    {0x88u,0xA4u,0,0,0,0},
    {0x89u,0xAFu,0,0,0,0},
    {0x8Au,0xC5u,0,0,0,0},
    {0x8Bu,0xD7u,0,0,0,0},
    {0x8Cu,0xE8u,0,0,0,0},
    {0x1Cu,0x7Fu,0,0,0,0},  /* MIDH: below 0x80 */
    {0x1Du,0xA2u,0,0,0,0},  /* MIDL: above 0x80 */
    {0x94u,0xB0u,0,0,0,0},
    {0x96u,0x13u,0,0,0,0},
    {0x99u,0x91u,0,0,0,0}
};
static void scan_msb(void)
{
    for (unsigned r=0;r<sizeof(msb_scan)/sizeof(msb_scan[0]);++r) {
        c3_msb_scan_t *n=&msb_scan[r];
        n->leg_ff=n->alt_ff=0u;
        for (unsigned j=0;j<5u;++j) {
            n->leg_last=ov7725_read_reg(n->reg);
            n->alt_last=ov7725_read_reg_restart(n->reg);
            if(n->leg_last==0xFFu) ++n->leg_ff;
            if(n->alt_last==0xFFu) ++n->alt_ff;
        }
    }
}

/* Optional DATA-bit-7 write/read experiment. Only invoked on page 4 by
 * an explicit SHUTTER press. 0x96 has a known-good low-bit7 value; 0x94
 * has a failing high-bit7 value. Write each register with both MSB=0 and
 * MSB=1 while keeping the low 7 bits identical. Both values are restored,
 * followed by a full sensor re-initialization. No frames/SD/Flash writes.
 * The SCCB write primitive does NOT prove successful writes: observe the
 * actual readback and do not interpret FF as a sensor register value.
 */
typedef struct {
    uint8_t reg, value;
    uint8_t l_last, a_last, l_ok, a_ok, l_ff, a_ff;
} c3_msb_probe_t;
static c3_msb_probe_t bit7_probe[] = {
    {0x96u,0x13u,0,0,0,0,0,0},
    {0x96u,0x93u,0,0,0,0,0,0},
    {0x94u,0x30u,0,0,0,0,0,0},
    {0x94u,0xB0u,0,0,0,0,0,0}
};
static uint8_t bit7_probe_ready=0u;
static uint8_t bit7_restore_init_code=0u;
static void run_bit7_probe(uint8_t raw)
{
    bit7_probe_ready=0u;
    for (unsigned r=0;r<sizeof(bit7_probe)/sizeof(bit7_probe[0]);++r) {
        c3_msb_probe_t *n=&bit7_probe[r];
        n->l_ok=n->a_ok=n->l_ff=n->a_ff=0u;
        (void)ov7725_write_reg(n->reg,n->value);
        HAL_Delay(4u);
        for (unsigned j=0;j<5u;++j) {
            n->l_last=ov7725_read_reg(n->reg);
            n->a_last=ov7725_read_reg_restart(n->reg);
            if(n->l_last==n->value) ++n->l_ok;
            if(n->a_last==n->value) ++n->a_ok;
            if(n->l_last==0xFFu) ++n->l_ff;
            if(n->a_last==0xFFu) ++n->a_ff;
        }
    }
    /* Explicitly restore both registers before the normal full reset.
     * In RAW8 mode a matrix readback error can make init return code 4,
     * even though it finished writing the complete register table. */
    (void)ov7725_write_reg(0x96u,0x13u);
    (void)ov7725_write_reg(0x94u,0xB0u);
    bit7_restore_init_code=raw ? ov7725_RAW_init() : ov7725_init();
    bit7_probe_ready=1u;
}

/* v0.1.49: phase sampler. This explicitly tests the same high/low bit7
 * pairs with two SDA release methods. No MCU Flash/SD writes; it temporarily
 * changes sensor registers 0x96/0x94 only on SHUTTER and reinitializes.
 * Expected values are informative; actual returned physical bits win.
 */
typedef struct {
    uint8_t reg, write_value;
    c3_sccb_trace_t od, in;
} c3_sccb_phase_item_t;
static c3_sccb_phase_item_t phases[] = {
    {0x1Du,0xA2u,{0,0,0},{0,0,0}}, /* MIDL: read-only reference */
    {0x96u,0x13u,{0,0,0},{0,0,0}},
    {0x96u,0x93u,{0,0,0},{0,0,0}},
    {0x94u,0x30u,{0,0,0},{0,0,0}},
    {0x94u,0xB0u,{0,0,0},{0,0,0}}
};
static uint8_t phase_ready = 0u;
static uint8_t phase_restore_init_code = 0u;
static void run_phase_probe(uint8_t raw)
{
    phase_ready=0u;
    for (unsigned i=0; i<sizeof(phases)/sizeof(phases[0]); ++i) {
        c3_sccb_phase_item_t *p=&phases[i];
        if (p->reg!=0x1Du) {
            (void)ov7725_write_reg(p->reg,p->write_value);
            HAL_Delay(4u);
        }
        sccb_trace_read_reg(OV7725_ADDR,p->reg,0u,&p->od);
        sccb_trace_read_reg(OV7725_ADDR,p->reg,1u,&p->in);
    }
    (void)ov7725_write_reg(0x96u,0x13u);
    (void)ov7725_write_reg(0x94u,0xB0u);
    phase_restore_init_code=raw ? ov7725_RAW_init() : ov7725_init();
    phase_ready=1u;
}

static char hexdigit(uint8_t d) { return (char)((d&15u)<10u ? '0'+(d&15u):'A'+(d&15u)-10u); }
static void hex2(char *p, uint8_t d) { p[0]=hexdigit(d>>4); p[1]=hexdigit(d); }

static void scan_all(void)
{
    for (unsigned r=0; r<sizeof(scan)/sizeof(scan[0]); ++r) {
        sccb_scan_item_t *i = &scan[r];
        i->l_good=i->r_good=i->l_ff=i->r_ff=0u;
        for (unsigned n=0;n<5u;++n) {
            uint8_t l = ov7725_read_reg(i->reg);
            uint8_t a = ov7725_read_reg_restart(i->reg);
            i->l_last=l;
            i->r_last=a;
            if (((l ^ i->expected) & i->mask)==0u) ++i->l_good;
            if (((a ^ i->expected) & i->mask)==0u) ++i->r_good;
            if (l==0xFFu) ++i->l_ff;
            if (a==0xFFu) ++i->r_ff;
        }
    }
}

static void draw_table(uint8_t raw, uint16_t half_us, uint8_t init_code)
{
    c3_lcd_fill(0x0000u);
    c3_lcd_text(10, 5, "C3M1 SCCB REGISTER SCAN", 0xFFFFu, 1);
    c3_lcd_text(10, 24, raw ? "RAW8 REGISTERS" : "RGB565 REGISTERS", 0xFFE0u, 1);
    char line[]="050us INIT 0";
    line[0]=(char)('0'+(half_us/100u)%10u);
    line[1]=(char)('0'+(half_us/10u)%10u);
    line[2]=(char)('0'+half_us%10u);
    line[11]=hexdigit(init_code);
    c3_lcd_text(200,24,line,0xFFFFu,1);
    /* Idle level alone does not prove correct waveforms, but catches a
     * hard-stuck SCL/SDA line after a completed transaction. */
    char idle[]="IDLE SCL0 SDA0";
    idle[8]=(GPIOD->IDR & GPIO_PIN_3) ? '1' : '0';
    idle[13]=(GPIOG->IDR & GPIO_PIN_13) ? '1' : '0';
    c3_lcd_text(10,35,idle,0xFFFFu,1);
    c3_lcd_text(10,45,"REG EX  LEG   OK F  ALT   OK F",0xFFFFu,1);
    for (unsigned r=0; r<sizeof(scan)/sizeof(scan[0]);++r) {
        const sccb_scan_item_t *i=&scan[r];
        /* The two methods are always scanned separately; OK=matches/5,
         * F=count of FF/5. A failing FF is NOT a successful register write. */
        char row[]="00 00  L00  0 0  R00  0 0";
        hex2(row+0,i->reg); hex2(row+3,i->expected);
        hex2(row+8,i->l_last); hex2(row+18,i->r_last);
        row[12]=hexdigit(i->l_good); row[14]=hexdigit(i->l_ff);
        row[22]=hexdigit(i->r_good); row[24]=hexdigit(i->r_ff);
        uint16_t col=(i->r_good==5u && i->l_good==5u) ? 0x07E0u :
                     (i->r_good>0u ? 0xFFE0u : 0xF800u);
        c3_lcd_text(10,62+(int)r*18,row,col,1);
    }
    c3_lcd_text(10,211,"OK=EXPECTED MATCHES /5",0xFFFFu,1);
    c3_lcd_text(10,226,"4X:MODE ISO:PAGE SHOT:AGAIN",0xFFE0u,1);
}

/* New page reports last raw bytes for every neighbor, regardless of
 * expected value. FF is counted separately from a comparison mismatch.
 * Reserved addresses may naturally read FF and are not "errors". */
static void draw_neighbors(uint8_t raw, uint16_t half_us, uint8_t init_code)
{
    c3_lcd_fill(0x0000u);
    c3_lcd_text(8,5,"C3M1 SCCB 0x8E-0x9C",0xFFFFu,1);
    c3_lcd_text(8,21,raw?"RAW8":"RGB565",0xFFE0u,1);
    char time[]="050us INIT 0";
    time[0]=(char)('0'+(half_us/100u)%10u);
    time[1]=(char)('0'+(half_us/10u)%10u);
    time[2]=(char)('0'+half_us%10u);
    time[11]=hexdigit(init_code);
    c3_lcd_text(140,21,time,0xFFFFu,1);
    c3_lcd_text(8,39,"REG LEG F/5 ALT F/5",0xFFFFu,1);
    for (unsigned r=0;r<sizeof(neighbor)/sizeof(neighbor[0]);++r) {
        const sccb_neighbor_t *n=&neighbor[r];
        char text[]="00  00 0/5 00 0/5";
        hex2(text,n->reg);
        hex2(text+4,n->leg_last);
        text[7]=(char)('0'+n->leg_ff);
        hex2(text+11,n->alt_last);
        text[14]=(char)('0'+n->alt_ff);
        uint16_t col=(n->leg_ff == 5u && n->alt_ff == 5u)
            ? 0xF800u : ((n->leg_ff || n->alt_ff)?0xFFE0u:0x07E0u);
        c3_lcd_text(8,54+(int)r*11,text,col,1);
    }
    c3_lcd_text(8,222,"F=FF READ COUNT /5",0xFFFFu,1);
    c3_lcd_text(8,230,"4X MODE ISO PAGE SHOT AGAIN",0xFFE0u,1);
}


/* Passive bit7 survey: gamma entries cross 0x7F -> 0x86, same read bus. */
static void draw_msb(uint8_t raw)
{
    c3_lcd_fill(0x0000u);
    c3_lcd_text(8,4,"C3M1 SCCB DATA BIT7",0xFFFFu,1);
    c3_lcd_text(8,19,raw?"RAW8":"RGB565",0xFFE0u,1);
    c3_lcd_text(8,35,"REG EXP LEG F ALT F",0xFFFFu,1);
    for (unsigned r=0;r<sizeof(msb_scan)/sizeof(msb_scan[0]);++r) {
        const c3_msb_scan_t *n=&msb_scan[r];
        char row[]="00 00  00 0 00 0";
        hex2(row,n->reg); hex2(row+3,n->expected);
        hex2(row+7,n->leg_last); row[10]=(char)('0'+n->leg_ff);
        hex2(row+12,n->alt_last); row[15]=(char)('0'+n->alt_ff);
        uint16_t col=(n->leg_ff==5u && n->alt_ff==5u)?0xF800u:
          ((n->leg_ff || n->alt_ff)?0xFFE0u:0x07E0u);
        c3_lcd_text(8,51+(int)r*12,row,col,1);
    }
    c3_lcd_text(8,224,"F=FF COUNTS / 5",0xFFFFu,1);
    c3_lcd_text(8,230,"ISO NEXT 4X MODE",0xFFE0u,1);
}
static void draw_bit7_probe(uint8_t raw)
{
    c3_lcd_fill(0x0000u);
    c3_lcd_text(8,4,"BIT7 A/B WRITE-READ",0xFFFFu,1);
    c3_lcd_text(8,23,raw?"RAW8 SENSOR":"RGB565 SENSOR",0xFFE0u,1);
    c3_lcd_text(8,42,"TEMP WRITES; AUTO RESTORE",0xFFE0u,1);
    if (!bit7_probe_ready) {
        c3_lcd_text(8,82,"SHUTTER: RUN BIT7 TEST",0xFFFFu,1);
    } else {
        c3_lcd_text(8,71,"REG SET LEG OK F ALT OK F",0xFFFFu,1);
        for (unsigned r=0;r<sizeof(bit7_probe)/sizeof(bit7_probe[0]);++r) {
            const c3_msb_probe_t *n=&bit7_probe[r];
            char row[]="00 00  00 0 0 00 0 0";
            hex2(row,n->reg); hex2(row+3,n->value);
            hex2(row+7,n->l_last); row[10]=(char)('0'+n->l_ok);
            row[12]=(char)('0'+n->l_ff); hex2(row+14,n->a_last);
            row[17]=(char)('0'+n->a_ok); row[19]=(char)('0'+n->a_ff);
            uint16_t col=(n->l_ok==5u && n->a_ok==5u)?0x07E0u:
              ((n->l_ff==5u && n->a_ff==5u)?0xF800u:0xFFE0u);
            c3_lcd_text(8,94+(int)r*22,row,col,1);
        }
        char status[]="RESTORE INIT 0";
        status[13]=hexdigit(bit7_restore_init_code);
        c3_lcd_text(8,199,status,0xFFFFu,1);
    }
    c3_lcd_text(8,226,"ISO NEXT 4X MODE",0xFFE0u,1);
}

static void draw_phase_probe(uint8_t raw)
{
    c3_lcd_fill(0x0000u);
    c3_lcd_text(8,4,"SCCB SDA PHASE / RELEASE",0xFFFFu,1);
    c3_lcd_text(8,20,raw?"RAW8 50us":"RGB565 50us",0xFFE0u,1);
    if (!phase_ready) {
        c3_lcd_text(8,84,"SHUTTER: RUN SDA PHASE",0xFFFFu,1);
        c3_lcd_text(8,100,"TEMP WRITES; RESTORES",0xFFE0u,1);
    } else {
        c3_lcd_text(8,34,"REG VAL OD L/E/H  IN L/E/H",0xFFFFu,1);
        for (unsigned i=0; i<sizeof(phases)/sizeof(phases[0]);++i) {
            const c3_sccb_phase_item_t *p=&phases[i];
            char line[]="00 00 OD 00/00/00";
            char line2[]="      IN 00/00/00";
            hex2(line+0,p->reg);
            hex2(line+3,p->write_value);
            hex2(line+9,p->od.low);
            hex2(line+12,p->od.early);
            hex2(line+15,p->od.late);
            hex2(line2+9,p->in.low);
            hex2(line2+12,p->in.early);
            hex2(line2+15,p->in.late);
            uint16_t fg=(p->od.late==p->write_value ||
                         p->in.late==p->write_value) ? 0x07E0u : 0xF800u;
            c3_lcd_text(8,49+(int)i*30,line,fg,1);
            c3_lcd_text(8,62+(int)i*30,line2,fg,1);
        }
        char result[]="RESTORE INIT 0";
        result[13]=hexdigit(phase_restore_init_code);
        c3_lcd_text(8,205,result,0xFFFFu,1);
    }
    c3_lcd_text(8,220,"L=LOW E=EARLY H=LATE",0xFFFFu,1);
    c3_lcd_text(8,230,"ISO NEXT 4X MODE SHOT RUN",0xFFE0u,1);
}

void c3_sccb_diag_run(void)
{
    const uint16_t half_us=50u; /* 50/100/200 had identical real-world failures. */
    uint8_t raw=1u, page=0u, init_code=0u;
    uint16_t prev=0u;
    c3_sccb_half_period_us=half_us;
    /* Refresh sensor state only when explicitly requested or mode toggled. */
    uint8_t refresh=1u;
    while (1) {
        if (refresh) {
            c3_sccb_half_period_us=half_us;
            sccb_init();
            init_code=raw ? ov7725_RAW_init() : ov7725_init();
            scan_all();
            scan_neighbors();
            scan_msb();
            bit7_probe_ready=0u;
            phase_ready=0u;
            refresh=0u;
        }
        if (page==0u) draw_table(raw,half_us,init_code);
        else if (page==1u) draw_neighbors(raw,half_us,init_code);
        else if (page==2u) draw_msb(raw);
        else if (page==3u) draw_bit7_probe(raw);
        else draw_phase_probe(raw);
        prev=(uint16_t)((~GPIOF->IDR) & C3_BTN_MASK);
        for (;;) {
            HAL_Delay(65u);
            uint16_t down=(uint16_t)((~GPIOF->IDR) & C3_BTN_MASK);
            uint16_t pressed=(uint16_t)(down & (uint16_t)~prev);
            prev=down;
            if (pressed & C3_BTN_4X) {raw^=1u;refresh=1u;break;}
            if (pressed & C3_BTN_ISO) {page=(uint8_t)((page+1u)%5u);break;}
            if (pressed & C3_BTN_SHUTTER) {
                if (page==3u) {
                    run_bit7_probe(raw);
                    scan_all(); scan_neighbors(); scan_msb();
                } else if (page==4u) {
                    run_phase_probe(raw);
                    scan_all(); scan_neighbors(); scan_msb();
                } else refresh=1u;
                break;
            }
        }
    }
}

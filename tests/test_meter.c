/* Host-side readback tests: no actual SCCB hardware required. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c3_meter.h"

typedef struct {
    uint8_t h,l,g;
    uint8_t fail_address;
    uint8_t bounce_once;
    unsigned high_reads;
    unsigned total_reads;
} virtual_sensor_t;
static virtual_sensor_t sensor;
static uint8_t reg_read(uint8_t address, uint8_t *v)
{
    ++sensor.total_reads;
    if (address == sensor.fail_address) return 0u;
    if (address == 0x08u) {
        ++sensor.high_reads;
        *v = (uint8_t)(sensor.h + (sensor.bounce_once && sensor.high_reads == 1u));
    } else if (address == 0x10u) *v = sensor.l;
    else if (address == 0x00u) *v = sensor.g;
    else return 0u;
    return 1u;
}
int main(void)
{
    assert(c3_meter_decode_gain_x16(0x00u)==16u);
    assert(c3_meter_decode_gain_x16(0x07u)==23u);
    assert(c3_meter_decode_gain_x16(0x0Fu)==31u);
    assert(c3_meter_decode_gain_x16(0x10u)==32u);
    assert(c3_meter_decode_gain_x16(0x30u)==64u);
    assert(c3_meter_decode_gain_x16(0x70u)==128u);
    assert(c3_meter_decode_gain_x16(0xF0u)==256u);
    assert(c3_meter_decode_gain_x16(0xFFu)==496u);
    c3_meter_t m;
    c3_meter_reset(&m);
    memset(&sensor,0,sizeof(sensor));
    sensor.h=1u; sensor.l=0x23u; sensor.g=0x37u;
    sensor.fail_address=0xAAu;
    assert(c3_meter_poll(&m,100u,reg_read));
    assert(m.valid && m.exposure_rows==0x123u && m.gain_x16==92u);
    unsigned reads=sensor.total_reads;
    assert(!c3_meter_poll(&m,449u,reg_read) && sensor.total_reads==reads);
    sensor.l=0xFEu; sensor.g=0x10u;
    assert(c3_meter_poll(&m,450u,reg_read));
    assert(m.exposure_rows==0x1FEu && m.gain_x16==32u);
    sensor.bounce_once=1u; sensor.high_reads=0u;
    assert(c3_meter_poll(&m,800u,reg_read));
    assert(m.valid && m.exposure_rows==0x1FEu && sensor.high_reads >=4u);
    sensor.fail_address=0x00u;
    assert(c3_meter_poll(&m,1150u,reg_read));
    assert(!m.valid); /* no stale reading after failing gain address */
    sensor.fail_address=0xAAu;
    assert(c3_meter_poll(&m,1500u,reg_read) && m.valid);
    assert(c3_meter_poll(&m,100u,reg_read)); /* wrap-safe elapsed */
    char e[12],g[12];
    c3_meter_exposure_label(e,123u,1u);
    assert(strcmp(e,"E123LA")==0);
    c3_meter_exposure_label(e,65535u,0u);
    assert(strcmp(e,"E65535L")==0);
    c3_meter_gain_label(g,40u,1u);
    assert(strcmp(g,"G2.5XA")==0);
    c3_meter_gain_label(g,256u,0u);
    assert(strcmp(g,"G16.0X")==0);
    c3_meter_gain_label(g,496u,1u);
    assert(strcmp(g,"G31.0XA")==0);
    puts("METER: gain decoding, coherent AEC reads, retry/errors, throttling, wrap and text PASS");
    return 0;
}

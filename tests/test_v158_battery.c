#include "c3_battery.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    assert(c3_battery_raw_to_mv(0)==0);
    assert(c3_battery_raw_to_mv(4095)==6600);
    assert(c3_battery_raw_to_mv(2048)>=3299);
    assert(c3_battery_raw_to_mv(2048)<=3302);
    uint8_t prev=0;
    for(unsigned mv=3200;mv<=4200;mv++) {
        uint8_t p=c3_battery_estimate_percent((uint16_t)mv);
        assert(p>=prev && p<=100);
        prev=p;
    }
    assert(c3_battery_estimate_percent(3200)==0);
    assert(c3_battery_estimate_percent(3700)==30);
    assert(c3_battery_estimate_percent(3800)==50);
    assert(c3_battery_estimate_percent(4200)==100);
    char label[16];
    c3_battery_format(label,3800);
    assert(strcmp(label,"3.80V ~50%") == 0);
    c3_battery_format(label,4200);
    assert(strcmp(label,"4.20V ~100%") == 0);
    c3_battery_format(label,2300);
    assert(strcmp(label,"BAT LOW") == 0);
    c3_battery_format_unknown(label);
    assert(strcmp(label,"ADC ERR") == 0);
    puts("Battery divider, rough SOC interpolation and UI format PASS");
    return 0;
}

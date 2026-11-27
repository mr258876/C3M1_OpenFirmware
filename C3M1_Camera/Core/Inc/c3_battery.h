#ifndef C3_BATTERY_H
#define C3_BATTERY_H
#include <stdint.h>

/* Main-board R17=10k from BAT+ and R18=10k to GND, midpoint PA1/ADC1_IN1.
 * These are ESTIMATES: VREF+ is assumed 3300 mV (not factory calibrated),
 * and terminal voltage alone cannot determine Li-ion state of charge. */
uint16_t c3_battery_raw_to_mv(uint16_t raw12);
uint8_t c3_battery_estimate_percent(uint16_t mv);
/* out requires >=16 bytes; text is "3.85V ~54%" or "ADC ERR". */
void c3_battery_format(char out[16], uint16_t mv);
void c3_battery_format_unknown(char out[16]);
/* Read only while SETTINGS is visible; false on ADC timeout/fault. */
uint8_t c3_battery_read_mv(uint16_t *mv);
#endif

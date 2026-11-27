#include "c3_board_pins.h"
#include "c3_buttons.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#define ISO C3_BTN_ISO
#define SHOT C3_BTN_SHUTTER
#define ZOOM C3_BTN_4X
#define FILM C3_BTN_FILM
int main(void)
{
    c3_buttons_t s;
    c3_buttons_reset(&s,0u,0u);
    c3_button_events_t e;
    e=c3_buttons_poll(&s,ISO,10u);
    assert(!e.pressed && !e.short_released);
    e=c3_buttons_poll(&s,ISO,31u);
    assert(e.pressed==ISO);
    e=c3_buttons_poll(&s,0u,130u);
    assert(!e.short_released);
    e=c3_buttons_poll(&s,0u,151u);
    assert(e.short_released==ISO && !e.long_pressed);
    e=c3_buttons_poll(&s,ISO,200u);
    e=c3_buttons_poll(&s,ISO,221u);
    assert(e.pressed==ISO);
    e=c3_buttons_poll(&s,ISO,900u);
    assert(e.long_pressed==ISO && !e.short_released);
    e=c3_buttons_poll(&s,ISO,1000u);
    assert(!e.long_pressed);
    (void)c3_buttons_poll(&s,0u,1010u);
    e=c3_buttons_poll(&s,0u,1031u);
    assert(!e.long_pressed && !e.short_released);
    (void)c3_buttons_poll(&s,ZOOM,1100u);
    (void)c3_buttons_poll(&s,ZOOM,1121u);
    e=c3_buttons_poll(&s,0u,1900u);
    assert(!e.long_pressed);
    e=c3_buttons_poll(&s,0u,1921u);
    assert(e.long_pressed==ZOOM && !e.short_released); /* slow poll */
    (void)c3_buttons_poll(&s,SHOT,2000u);
    e=c3_buttons_poll(&s,SHOT,2021u);
    assert(e.pressed==SHOT);
    (void)c3_buttons_poll(&s,0u,2090u);
    e=c3_buttons_poll(&s,0u,2111u);
    assert(e.short_released==SHOT);
    (void)c3_buttons_poll(&s,FILM,2200u);
    (void)c3_buttons_poll(&s,FILM,2221u);
    (void)c3_buttons_poll(&s,0u,2310u);
    e=c3_buttons_poll(&s,0u,2331u);
    assert(e.short_released==FILM);
    c3_buttons_reset(&s,0u,UINT32_MAX-40u);
    (void)c3_buttons_poll(&s,ISO,UINT32_MAX-30u);
    e=c3_buttons_poll(&s,ISO,UINT32_MAX-5u);
    assert(e.pressed==ISO);
    e=c3_buttons_poll(&s,ISO,680u);
    assert(e.long_pressed==ISO); /* SysTick wrap */
    printf("Buttons verified: debounce, short/long isolation, shutter press, rollover\n");
    return 0;
}

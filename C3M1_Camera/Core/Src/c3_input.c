#include "c3_input.h"
#include "c3_board_pins.h"
#include "main.h"

#define ISO C3_BTN_ISO
#define ZOOM C3_BTN_4X
#define FILM C3_BTN_FILM
#define INPUT_MASK C3_BTN_MASK

/* Debounce state is private to SysTick; 16-bit event latches are atomic on
 * Cortex-M3. Foreground takes/clears them with interrupts briefly masked. */
static c3_buttons_t scanner;
static volatile uint16_t ev_pressed, ev_released, ev_short;
static volatile uint16_t ev_long, ev_long_up, ev_chord;
static volatile uint16_t stable_mask;
static volatile uint8_t started;
static uint8_t divider;

static uint32_t enter_critical(void)
{
    uint32_t ps = __get_PRIMASK();
    __disable_irq();
    return ps;
}
static void exit_critical(uint32_t ps)
{
    if ((ps & 1u) == 0u) __enable_irq();
}

void c3_input_reset(uint16_t pressed_mask, uint32_t now_ms)
{
    uint32_t ps=enter_critical();
    started=0u;
    c3_buttons_reset(&scanner, (uint16_t)(pressed_mask & INPUT_MASK), now_ms);
    stable_mask=(uint16_t)(pressed_mask & INPUT_MASK);
    ev_pressed=ev_released=ev_short=ev_long=ev_long_up=ev_chord=0u;
    divider=0u;
    started=1u;
    exit_critical(ps);
}

void c3_input_sample(uint16_t pressed_mask, uint32_t now_ms)
{
    if (!started) return;
    c3_button_events_t e=c3_buttons_poll(&scanner,
                                   (uint16_t)(pressed_mask & INPUT_MASK), now_ms);
    stable_mask=c3_buttons_stable_mask(&scanner);
    ev_pressed |= e.pressed;
    ev_released |= e.released;
    ev_short |= e.short_released;
    ev_long |= e.long_pressed;
    ev_long_up |= e.long_released;
    if (stable_mask & FILM)
        ev_chord |= (uint16_t)((stable_mask | e.pressed |
                                e.short_released | e.long_pressed) & (ISO|ZOOM));
}

c3_input_batch_t c3_input_take(void)
{
    c3_input_batch_t b;
    uint32_t ps=enter_critical();
    b.events.pressed=ev_pressed;
    b.events.released=ev_released;
    b.events.short_released=ev_short;
    b.events.long_pressed=ev_long;
    b.events.long_released=ev_long_up;
    b.stable=stable_mask;
    b.film_chords=ev_chord;
    ev_pressed=ev_released=ev_short=ev_long=ev_long_up=ev_chord=0u;
    exit_critical(ps);
    return b;
}

void c3_input_systick(void)
{
    if (!started) return;
    if (++divider < 5u) return;
    divider=0u;
    c3_input_sample((uint16_t)(~GPIOF->IDR) & INPUT_MASK, HAL_GetTick());
}

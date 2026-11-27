#ifndef C3_BOARD_PINS_H
#define C3_BOARD_PINS_H
/* C3M1 PRODUCTION SCHEMATIC: 主板电路图.pdf, board1 updated 2026-05-22;
 * 顶板电路图.pdf, board2 updated 2026-03-23. On the STM32F103ZET6:
 *  PF0 = NC; PF1 = KEYISO; PF2 = KEYSHOT; PF3 = KEY4X;
 *  PF4 = LCDEN; PF5 = LCDRST; PF10 = FILMKEY.
 * Buttons are active LOW, with pull-ups on the top board.
 * IMPORTANT: PF3 MUST BE AN INPUT! It is shorted to GND by the 4X switch.
 */
#define C3_BTN_ISO       (1u << 1)
#define C3_BTN_SHUTTER   (1u << 2)
#define C3_BTN_4X        (1u << 3)
#define C3_BTN_FILM      (1u << 10)
#define C3_BTN_MASK (C3_BTN_ISO | C3_BTN_SHUTTER | C3_BTN_4X | C3_BTN_FILM)
/* LCD_EN is active high via a transistor; LCD_RESET is active low. */
#define C3_LCD_EN_PIN    (1u << 4)
#define C3_LCD_RESET_PIN (1u << 5)
#endif

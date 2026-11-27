#ifndef C3_RGB_TIMING_H
#define C3_RGB_TIMING_H
/* EXHCL (0x2B) sets EXTRA HORIZONTAL dummy pixels in OV7725 RGB preview.
 * Not vertical dummy rows: DM_LNL/DM_LNH and ADVFL/ADVFH remain at reset 0.
 * With v177 timing enabled, this is the RGB init baseline only. Manual
 * exposure updates EXHCH/EXHCL/CLKRC dynamically, including RAW stills.
 * Keep the baseline consistent with RGB init and timed register restore.
 */
#endif

C3M1 OpenFirmware v0.1.10 host-side checks
------------------------------------------
run_host_tests.sh validates 4x zoom crop geometry and in-place scanline
expansion; SysTick-style 200Hz input event latching (fast short presses,
FILM long hold and chord suppression); P/S/M and rotary menu navigation;
1x/4x OSD glyph placement and cache invalidation; streaming focus/FPS,
settings CRC and power-loss records, DNG data structure, preview scheduler,
exposure register combinations and RAW/SD recovery fault injection.

It is NOT a faithful electrical emulator of ST7789/FSMC writes, AL422B
refresh/read-write pointer timing, STM32 flash erase/program voltage/timing,
OV7725 sensor frame rate, or real input GPIO voltage levels. A physical
camera, SWD and ideally a logic analyser are still needed for release.

v0.1.15: test_storage_retry additionally validates BMP/RAW common numbering,
BMP media failure -> protected FIFO rewind -> successful retry. It emits an
artificial BMP to a temporary directory. check_bmp.py independently opens
that BMP with Pillow and verifies 24-bit top-down geometry and pixel samples.
Settings flash record now has version 2/9-byte payload; the target-only flash
module accepts v1 records for migration.

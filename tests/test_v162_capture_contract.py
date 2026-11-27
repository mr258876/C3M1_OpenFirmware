from pathlib import Path
r=Path(__file__).resolve().parent.parent
src=(r/'C3M1_Camera/Core/Src')
cam=(src/'c3_camera.c').read_text()
main=(src/'main.c').read_text()
ov=(r/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
build=(r/'build_clang.sh').read_text()
fast=cam.split('uint8_t c3_camera_bmp_fast_init(void)',1)[1].split('uint8_t c3_camera_bmp_init(void)',1)[0]
assert 'scheduler_stop();' in fast and 'c3_sched_reset(&frame_sched, C3_SCHED_STILL)' in fast
assert 'ov7725_init(' not in fast and 'ov7725_write_reg(' not in fast
assert 'c3_camera_preview_rearm();' in main.split('if (fast_bmp_captured)',1)[1]
assert 'C3_FAST_BMP_ENABLE &&' in main and 'pending_format == C3_OUTPUT_BMP && !preview_zoom4x' in main
assert 'c3_exposure_snapshot_read(&shot, ov7725_read_reg_checked)' in main
assert main.index('c3_exposure_snapshot_read(&shot, ov7725_read_reg_checked)') < main.index('c3_camera_raw_init()', main.index('c3_exposure_snapshot_read(&shot, ov7725_read_reg_checked)'))
assert 'C3_DNG_SETTLE_LOCKED' in main and 'fast_bmp ? 0u' in main
# v177 uses the snapshot gain, but RAW manual shutter must retain its own AEC table.
assert 'c3_exposure_apply_raw_locked_timed(&shot,&exposure' in main
assert 'c3_exposure_apply_raw_locked(' in (src/'c3_exposure.c').read_text()
assert 'C3_RAW_CLKRC' in build and 'C3_RAW_CLKRC != 0u' in ov
assert 'if (reg == CLKRC) val=(uint8_t)C3_RAW_CLKRC;' in ov
assert 'C3_DNG_SETTLE_LOCKED:-2' in build
# Baseline hardware safety constraints: GPIO reader, picture dimensions and RAW verification unchanged.
assert 'GPIOB->BRR = GPIO_PIN_4;' in (r/'C3M1_Camera/Core/Inc/c3_camera.h').read_text()
assert 'if (!verify_static_reg(MTX1, 0xB0u, 0xFFu)) return 4u;' in ov
assert 'if (!verify_static_reg(DSP_Ctrl4, 0x02u, 0x03u)) return 4u;' in ov
print('PASS v162: 1X fast BMP, RAW exposure inheritance/fallback, selectable RAW clock, unchanged FIFO/RAW strict gate')

from pathlib import Path
r=Path(__file__).resolve().parent.parent
main=(r/'C3M1_Camera/Core/Src/main.c').read_text()
expo=(r/'C3M1_Camera/Core/Src/c3_exposure.c').read_text()
ov=(r/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text(encoding='latin1')
osd=(r/'C3M1_Camera/Core/Src/c3_osd.c').read_text()
build=(r/'build_v177.sh').read_text()
assert build.count('C3_SHUTTER_DYNAMIC_TIMING=1')==1
assert 'C3_DNG_SOFT_PROFILE=1' in build
assert 'C3_SDIO_TX_DMA=1' in build
assert 'C3_FAST_RAW_SWITCH=1' in build and 'C3_FAST_RGB_RESTORE=1' in build
# Every current sensor re-init path needs a plan before the existing AEC writes.
assert main.count('c3_exposure_prepare_timing(')>=6
assert main.count('c3_exposure_apply(')>=6
assert 'c3_camera_preview_stop();\n        if (c3_exposure_prepare_timing(' in main
assert 'c3_exposure_apply_raw_locked_timed(&shot,&exposure' in main
assert 'c3_exposure_rows_for_mode(dial,C3_EXPOSURE_RAW,raw_clkrc)' in expo
assert 'c3_exposure_raw_rows(shot->preview_rows,raw_clkrc)' in expo
# Dynamic EXHCH preserves output-size low bits and checks actual SCCB readback.
assert '(original&0x0Fu)' in expo and '(original&0x0Fu)!=0u' in expo
assert 'wr(0x2Au,new_exh)' in expo and 'wr(0x2Bu,(uint8_t)p.dummy)' in expo
assert 'wr(0x11u,p.clkrc)' in expo
assert 'rd(0x2Au,&hi)' in expo and 'rd(0x2Bu,&lo)' in expo
assert 'hi==new_exh && lo==(uint8_t)p.dummy && clk==p.clkrc' in expo
# 4X fast preview must not leave CLKRC[6] set entering RAW no-reset.
assert 'case CLKRC: return 0u; /* preview 4X/8000 can be CLKRC=0x40 */' in ov
assert 'verify_static_reg(CLKRC,0x00u' in ov and '0x7Fu' in ov
# OSD uses current dial and zoom (not compile-time EXHCL0x80 only).
assert 'c3_exposure_preview_row_ns_dial(zoom4x,shutter_code)' in osd
assert 'C3_TIMING_RGB4X' in expo and 'C3_TIMING_RAW8' in expo
# Do not change still RAW geometry / file format from the established v173 path.
assert 'c3_camera_raw_fast_init()' in main
assert 'c3_dng_capture_preview_awb(ov7725_read_reg_checked)' in main
assert 'c3_storage_save(pending_format)' in main
print('PASS v177 mainline integration: FIFO stop/rearm, 12-bit dummy, RAW clock reset, snapshots, DNG/OSD')

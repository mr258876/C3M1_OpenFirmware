from pathlib import Path
p=Path(__file__).resolve().parents[1]
main=(p/'C3M1_Camera/Core/Src/main.c').read_text(encoding='latin1')
cam=(p/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text(encoding='latin1')
dng=(p/'C3M1_Camera/Core/Src/c3_dng.c').read_text(encoding='latin1')
exp=(p/'C3M1_Camera/Core/Src/c3_exposure.c').read_text()
b=(p/'build_clang.sh').read_text()
# The RAW snapshot is taken with preview RGB registers still installed.
shot=main.index('/* Read AWB ONLY in live RGB565 preview.')
assert shot<main.index('c3_camera_raw_fast_init()',shot)
assert main.count('(void)c3_dng_capture_preview_awb(ov7725_read_reg_checked);')>=1
assert 'c3_dng_begin_shot();' in dng and 'captured_sensor_awb[0]' in dng
assert 'if (reg == COM8) val = 0x00u;' in cam
assert 'if (reg == COM8) val=0x00u;' in cam
assert '(void)ov7725_write_reg(COM8, 0x00u)' in cam
assert 'C3_RAW_FREEZE_AWB:-1' in b
assert '#if C3_RAW_FREEZE_AWB' in exp
assert 'C3_DNG_SETTLE_LOCKED:-2' in b, 'Do not remove frame settling for RAW/FIFO pipeline'
print('PASS v172 AWB: snapshot before RAW, disabled RAW auto white balance, frame settlement preserved')

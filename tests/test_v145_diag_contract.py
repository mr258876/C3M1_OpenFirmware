#!/usr/bin/env python3
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
scan=(root/'C3M1_Camera/Core/Src/c3_sccb_diag.c').read_text()
main=(root/'C3M1_Camera/Core/Src/main.c').read_text()
ov=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
for reg in ['0x0Au','0x0Bu','0x1Cu','0x1Du','0x12u','0x67u','0x94u','0x95u']:
    assert reg in scan,reg
for s in ['C3_BTN_ISO','C3_BTN_SHUTTER','C3_BTN_4X','ov7725_RAW_init()', 'ov7725_init()','ov7725_read_reg_restart','c3_lcd_text']:
    assert s in scan,s
assert 'c3_sccb_diag_run();' in main
assert 'if (exhch != 0xFFu)' in ov
assert 'C3_RAW_VERIFY_WARN_IF_FF' in ov
assert '#if C3_SCCB_RESTART_VERIFY' in ov
candidates = [root/'build_preview_v146_Scan_50_100_200/c3m1.elf',
              root/'build_preview/c3m1.elf',
              root/'build_preview_v145_scan/c3m1.elf']
p=next((p for p in candidates if p.exists()),None)
if p is None:
    print('SCCB scan ELF isolation skipped (build the scanner first)')
    raise SystemExit(0)
r=subprocess.run(['readelf','-Ws',str(p)],check=True,capture_output=True,text=True)
names=r.stdout
assert 'c3_sccb_diag_run' in names
# Full LTO may inline RAW init into c3_sccb_diag_run; source check above guards it.
# Runtime code absent from readonly scan; don't mistake const strings for code.
for symbol in ['c3_storage_save','c3_settings_flash_save','c3_dng_write','HAL_SD_WriteBlocks']:
    assert symbol not in names, symbol
print('SCCB scan diagnostic build: matrix + read-only ELF isolation OK')

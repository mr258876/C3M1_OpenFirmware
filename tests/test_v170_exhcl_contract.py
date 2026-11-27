from pathlib import Path
p=Path(__file__).resolve().parents[1]
b=(p/'build_clang.sh').read_text()
reg=(p/'C3M1_Camera/Drivers/BSP/OV7725/ov7725cfg.h').read_bytes()
exp=(p/'C3M1_Camera/Core/Src/c3_exposure.c').read_text()
assert 'C3_SDIO_TX_DMA:-1' in b
# v177 boots at the field-tested 0x80; 0x9E remains available as an opt-out.
assert 'C3_RGB_EXHCL:-0x80' in b
assert b'{EXHCL,     C3_RGB_EXHCL}' in reg
assert b'{EXHCL,     0x00}' in reg
assert '((576u + C3_RGB_EXHCL) * 2u)' in exp
assert '((784u + C3_RGB_EXHCL) * 2u)' in exp
# No extra vertical dummy line writes were added by this release.
assert b'{DM_LNL,' not in reg and b'{DM_LNH,' not in reg
assert b'{ADVFL,' not in reg and b'{ADVFH,' not in reg
print('PASS v170 invariants: default SDIO DMA; v177 baseline EXHCL80; old EXHCL math retained for rollback')

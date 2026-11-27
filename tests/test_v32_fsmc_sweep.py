"""Checks v0.1.32 controlled FSMC-only LCD DMA speed sweep artifacts.

Does not replace hardware timing tests. Designed to be run after producing all
four outputs with C3_LCD_DMA_ENABLE=1 C3_FSMC_DATAST=N bash build_clang.sh.
"""
from hashlib import sha256
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = (root / "C3M1_Camera/Core/Src/fsmc.c").read_text()
build = (root / "build_clang.sh").read_text()

assert "#define C3_FSMC_DATAST 4" in src
assert "t.AddressSetupTime = 2;" in src
assert "t.DataSetupTime = C3_FSMC_DATAST;" in src
assert "t.AccessMode = FSMC_ACCESS_MODE_A;" in src
assert "hsram1.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;" in src
assert "hsram1.Init.ExtendedMode = FSMC_EXTENDED_MODE_DISABLE;" in src
assert "C3_FSMC_DATAST must be 8, 6, 5 or 4" in src
assert '"-DC3_FSMC_DATAST=${C3_FSMC_DATAST}"' in build
assert "C3_LCD_DMA_ENABLE:-1" in build
assert "Core/Src/c3_lcd_dma.c" in build

expected_v31_dma_bin_sha256 = (
    "a9f34f269cae99f7d026ce2e99c680a41fcab29fa6fa903d63aaa0db4ecd24f6"
)
checksums = {}
for datast in (8, 6, 5, 4):
    prefix = root / "firmware_fsmc" / f"C3M1_v0_1_32_DMA_FSMC_D{datast}"
    for ext in (".bin", ".hex", ".elf"):
        assert prefix.with_suffix(ext).is_file(), (prefix, ext)
    image = prefix.with_suffix(".bin").read_bytes()
    assert 35_000 < len(image) < 65_000
    checksums[datast] = sha256(image).hexdigest()
assert len(set(checksums.values())) == 4, checksums
assert checksums[8] == expected_v31_dma_bin_sha256, (
    "v0.1.31 DMA control is not byte-identical", checksums[8]
)
print("PASS: fixed ADDSET=2 / DMA=1; D8, D6, D5, D4 distinct and baseline D8 exactly v0.1.31")
for datast, checksum in checksums.items():
    print(f"  DATAST={datast}: {checksum}")

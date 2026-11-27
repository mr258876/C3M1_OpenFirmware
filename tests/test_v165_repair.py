"""Validate header-only D50/tint repair of a real v0.1.64 generated test DNG."""
from pathlib import Path
import importlib.util
import struct
import sys
p=Path(__file__).resolve().parents[1]
name=p/'tools/repair_v164_tint_dng.py'
spec=importlib.util.spec_from_file_location('repair', name)
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
if len(sys.argv)!=2:
    raise SystemExit('Need a v164 mock DNG as argv[1]')
orig=Path(sys.argv[1]).read_bytes()
fixed, before, after=mod.edit_v164_dng(orig)
assert len(orig)==len(fixed)==308672
assert orig[512:]==fixed[512:]
assert before==(1.045,.533) and after==(1.045,.533)
assert fixed!=orig
# Sensor AWB case with non-reference green/magenta:
changed=bytearray(orig)
def tag_value(tag):
    count=struct.unpack_from('<H',changed,8)[0]
    for i in range(count):
        off=10+12*i
        if struct.unpack_from('<H',changed,off)[0]==tag:
            return struct.unpack_from('<I',changed,off+8)[0]
    raise AssertionError(tag)
npos=tag_value(50728)
struct.pack_into('<I',changed,npos,128000)
struct.pack_into('<I',changed,npos+16,53333)
dynamic, old_val, new_val=mod.edit_v164_dng(bytes(changed))
assert old_val==(1.28,.53333)
assert 1.044 < new_val[0] < 1.045 and .435 < new_val[1] < .436
assert dynamic[512:]==orig[512:]
assert abs((new_val[0]/new_val[1])-(old_val[0]/old_val[1]))<.001
assert (lambda b: next(struct.unpack_from('<I',b,10+12*i+8)[0] for i in range(struct.unpack_from('<H',b,8)[0]) if struct.unpack_from('<H',b,10+12*i)[0]==50778))(fixed)==23
try:
    mod.edit_v164_dng(fixed)
except ValueError:
    pass
else:
    raise AssertionError('Must refuse double patch')
print('PASS v165: v164 patch script changes header only, rejects double patch')

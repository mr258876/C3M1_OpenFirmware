from pathlib import Path
import hashlib, subprocess, tempfile, struct
root=Path(__file__).resolve().parents[1]
core=root/'C3M1_Camera/Core/Src'
main=(core/'main.c').read_text()
ov=(root/'C3M1_Camera/Drivers/BSP/OV7725/ov7725.c').read_text()
init=ov[ov.index('uint8_t ov7725_init(void)'):ov.index('uint8_t ov7725_RAW_init(void)')]
assert init.index('if(ov7725_write_reg(COM8,0xF8u)||ov7725_write_reg(COM5,0u))')<init.index('for (i = 0; i < sizeof(ov7725_init_reg_tb1)')
assert 'if(addr==COM8) value=0xF8u;' in init and 'if(addr==COM5) value=0u;' in init
assert 'reg==COM5?0u:' in ov
resume=main[main.index('static void resume_preview('):main.index('static void resume_preview_controls(')]
assert 'c3_camera_preview_stop();' in resume and 'zoom_release();' in resume
assert resume.index('zoom_apply_preview(exposure)')<resume.index('if (*camera_ok) c3_camera_preview_rearm();')
controls=main[main.index('static void resume_preview_controls('):main.index('static void resume_preview_after_raw(')]
assert 'c3_zoom_hold_matches(&zoom_hold,exposure)' in controls
assert 'zoom_release();resume_preview(camera_ok,fps,exposure,meter);return;' in controls
assert 'if(exposure_changed && preview_zoom4x)' in main
assert main.index('c3_zoom_hold_read(&zoom_hold,&exposure')<main.index('preview_zoom4x=1u;')
assert 'zoom_hold.source.valid?c3_zoom_hold_rows(&zoom_hold,C3_EXPOSURE_RAW)' in main
assert 'zoom_hold.source.valid ? c3_zoom_hold_apply(&zoom_hold,' in main
assert '&& !zoom_hold.source.valid' in main
assert 'c3_dng_capture_locked_awb(zoom_hold.red,zoom_hold.green,zoom_hold.blue)' in main
assert 'if(zoom_hold.source.valid)meter.warning=zoom_hold.warning;' in main
assert 'zoom_release(); /* wake starts a fresh 1X metering session */' in main
assert 'if(zoom_hold.source.valid){\n            if(zoom_apply_preview(exposure))' in main
for variant in ('ZOOM','ZOOM_DIAG'):
    out=root/f'build_V204_{variant}_ACR8_F2'
    image={};base=0;ended=False
    for line in (out/'c3m1.hex').read_text().splitlines():
        b=bytes.fromhex(line[1:]);assert line.startswith(':') and len(b)==b[0]+5 and sum(b)%256==0
        typ=b[3];addr=int.from_bytes(b[1:3],'big')
        if typ==4:base=int.from_bytes(b[4:6],'big')<<16
        elif typ==0:
            for i,v in enumerate(b[4:4+b[0]]):assert base+addr+i not in image;image[base+addr+i]=v
        elif typ==1:ended=True;break
        elif typ==5:pass
        else:raise AssertionError(typ)
    assert ended and min(image)==0x08000000
    raw=bytes(image[x] for x in range(min(image),max(image)+1))
    assert raw==(out/'c3m1.bin').read_bytes()
    assert struct.unpack_from('<I',raw)[0]==0x20010000
    assert 0x08000000<=struct.unpack_from('<I',raw,4)[0]<0x08000000+len(raw)
    with tempfile.TemporaryDirectory() as d:
        p=Path(d)/'fromelf.bin';subprocess.run(['llvm-objcopy','-O','binary',str(out/'c3m1.elf'),str(p)],check=True);assert p.read_bytes()==raw
    syms=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
    for n in ('zoom_hold','c3_zoom_hold_apply','c3_zoom_hold_rows','c3_camera_capture','f_write','live_raw','live_out'):
        assert n in syms,n
    assert b'4X LOCK FAIL\0' in raw
    assert (b'C5 -- ADV ---- DM ---- C8 -- V---\0' in raw)==(variant=='ZOOM_DIAG')
    print(f'PASS {variant}: {len(raw)} B HEX/BIN/ELF match; SHA256 {hashlib.sha256(raw).hexdigest()}')
print('PASS source integration guards: RGB init suppresses transient auto/night, 4X snapshot before zoom, hold through still and restore, release on control/wake, no crop-meter warning')

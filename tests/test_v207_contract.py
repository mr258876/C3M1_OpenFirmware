from pathlib import Path
import subprocess,tempfile,hashlib,struct
root=Path(__file__).resolve().parents[1]
core=root/'C3M1_Camera/Core/Src';main=(core/'main.c').read_text();play=(core/'c3_playback.c').read_text()
assert main.index('if (c3_recovery_can_retry(&recovery))')<main.index('if (change & C3_NAV_PLAY_OPEN)')
assert 'c3_camera_preview_stop();\n                sd_ok=c3_playback_enter' in main
assert 'if (nav.page==C3_UI_PLAY)' in main
assert main.index('if (change & C3_NAV_BACK)')>main.index('if (nav.page==C3_UI_PLAY)')
assert 'resume_preview_controls(&camera_ok,&fps,&exposure,&meter);\n                suppress_preview_keys = read_buttons();' in main
assert 'f_open(&file,path,FA_READ)' in play and 'f_close(&file)' in play
for forbidden in ['f_write(', 'f_unlink(', 'f_rename(', 'f_mkdir(', 'FA_WRITE','FA_CREATE']:
    assert forbidden not in play
for variant in ['PLAY','REF']:
    out=root/f'build_V207_{variant}_ACR8_F2';image={};base=0;ended=False
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
    raw=bytes(image[x] for x in range(min(image),max(image)+1));assert raw==(out/'c3m1.bin').read_bytes()
    assert struct.unpack_from('<I',raw)[0]==0x20010000
    assert 0x08000000<=struct.unpack_from('<I',raw,4)[0]<0x0807f000
    assert max(image)<0x0807f000
    with tempfile.TemporaryDirectory() as d:
        p=Path(d)/'fromelf.bin';subprocess.run(['llvm-objcopy','-O','binary',str(out/'c3m1.elf'),str(p)],check=True);assert p.read_bytes()==raw
    syms=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
    for n in ('c3_storage_save','c3_camera_capture','live_raw','live_out','zoom_hold'):
        assert n in syms,n
    for n in ('c3_playback_enter','c3_play_path','cached_read'):
        assert (n in syms)==(variant=='PLAY'),n
    assert (b'ISO - PLAY   SHOT - BACK\0' in raw)==(variant=='PLAY')
    print(f'PASS {variant}: {len(raw)} B HEX/BIN/ELF exact, vectors/settings partition, symbols and strings; SHA256 {hashlib.sha256(raw).hexdigest()}')
print('PASS integration: modal entry after held-still recovery, FIFO stopped, return to preview via old controls, read-only card access')

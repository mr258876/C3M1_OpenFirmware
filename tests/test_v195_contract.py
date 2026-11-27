from pathlib import Path
import re,subprocess,struct,hashlib
root=Path(__file__).resolve().parent.parent
src=(root/'C3M1_Camera/Core/Src/c3_fifo_dma_margin.inc').read_text()
old=(root/'C3M1_Camera/Core/Src/c3_fifo_dma_probe.c').read_text()
main=(root/'C3M1_Camera/Core/Src/main.c').read_text()
assert '#elif C3_FIFO_DMA_PROBE && C3_FIFO_DMA_MARGIN\n#include "c3_fifo_dma_margin.inc"\n#elif C3_FIFO_DMA_PROBE' in old
line=src[src.index('static uint8_t dma_line'):src.index('static uint32_t crc_row')]
assert line.index('TIM4->SMCR=0u;') < line.index('TIM3->EGR=TIM_EGR_UG;') < line.index('TIM4->SMCR=TIM_SMCR_TS_1')
assert line.index('DMA1_Channel2->CCR=0u;') < line.index('DMA1_Channel2->CPAR=') < line.index('DMA_CCR_EN;') < line.index('TIM3->CR1=TIM_CR1_CEN;')
assert 'TIM3->DIER=TIM_DIER_CC3DE;' in line and 'DMA1_Channel2->CPAR=(uint32_t)(uintptr_t)&GPIOG->IDR;' in line
assert 'DMA_CCR_PSIZE_1|DMA_CCR_MSIZE_0|DMA_CCR_MINC' in line
assert 'if(quiet) {' in line and 'elapsed_wait(c3_fm_quiet_cycles(*p));' in line
quiet=line[line.index('if(quiet) {'):line.index('} else while')]; assert not re.search(r'\b(?:TIM[34]|DMA1(?:_Channel2)?|GPIO[BG])->',quiet)
assert line.index('uint8_t status=c3_fd_check') < line.rindex('TIM3->CR1=0u;')
assert 'c3_camera_raw_rewind()' in src and 'c3_camera_raw_discard()' in src
assert 'row_crc[y]=c;' in src and 'c!=row_crc[y] || exact' in src and 'memcpy(bad_sample,row,C3_FD_BYTES);' in src
assert 'crc_row()!=row_crc[r->first_row]' in src and 'c3_fm_diff(row,bad_sample)' in src
assert 'post_ok=(bad==0u && post_crc==baseline_crc);' in src
assert 'for(int mode=1;mode>=0;mode--)' in src
assert 'c3_fifo_dma_probe_run(camera_ok);' in main and main.index('c3_fifo_dma_probe_run(camera_ok);') < main.index('MX_SDIO_SD_Init();',main.index('c3_fifo_dma_probe_run(camera_ok);'))
assert not any(s in src for s in ['f_write(', 'f_mount(', 'c3_storage_', 'HAL_FLASH_', 'c3_settings_flash_save('])
# Ensure every formatting write remains in its placeholder; test all literal buffers.
nspan=0
for block in re.split(r'(?=\bchar \w+\[\]=")',src)[1:]:
    m=re.match(r'char (\w+)\[\]="([^"]*)";',block)
    if not m: continue
    name,tmpl=m.groups()
    for kind,start,ln in re.findall(r'\b(dec|hex|tenths)\('+name+r'\+(\d+),[^;]*?(?:,(\d+))?\);',block):
        size=5 if kind=='tenths' else int(ln); off=int(start)
        assert len(tmpl[off:off+size])==size and all(x in '0.' for x in tmpl[off:off+size]),(name,tmpl,off,size)
        nspan+=1
assert nspan>=30,nspan
out=root/'build_V195_DMA_MARGIN_ACR8_F2'
sym=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
assert 'c3_fifo_dma_probe_run' in sym
for bad in ['f_mount','f_write','c3_storage_mount','HAL_FLASH_Program','HAL_FLASHEx_Erase','c3_settings_flash_save']:
    assert bad not in sym,bad
base=0;data={}
for l in (out/'c3m1.hex').read_text().splitlines():
    b=bytes.fromhex(l[1:]);assert l[0]==':' and len(b)==b[0]+5 and sum(b)%256==0
    n,a,ty=b[0],int.from_bytes(b[1:3],'big'),b[3]
    if ty==4:base=int.from_bytes(b[4:6],'big')<<16
    elif ty==0:
        for i,v in enumerate(b[4:4+n]):data[base+a+i]=v
    elif ty==1:break
    elif ty==5:pass
    else:raise AssertionError(ty)
assert min(data)==0x08000000
raw=bytes(data[x] for x in range(min(data),max(data)+1))
assert raw==(out/'c3m1.bin').read_bytes() and struct.unpack_from('<I',raw)[0]==0x20010000
assert 0x08000000<=struct.unpack_from('<I',raw,4)[0]<0x08000000+len(raw)
print(f'PASS: read-only linked image, gate/rearm order, no APB polling in QUIET, same reference for both modes, {nspan} UI format spans, HEX/BIN {len(raw)} bytes, SHA256 {hashlib.sha256(raw).hexdigest()}')

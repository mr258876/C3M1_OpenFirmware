"""Source/linked-image checks; cannot emulate actual F1 DMA or timer fabric."""
from pathlib import Path
import subprocess,sys,struct,hashlib
root=Path(__file__).resolve().parent.parent
src=(root/'C3M1_Camera/Core/Src/c3_fifo_dma_probe.c').read_text()
main=(root/'C3M1_Camera/Core/Src/main.c').read_text()
assert 'TIM4->SMCR=0u;' in src and 'TIM3->EGR=TIM_EGR_UG;' in src
line=src[src.index('static uint8_t dma_line'):src.index('static uint32_t crc_row')]
assert line.index('TIM4->SMCR=0u;') < line.index('TIM3->EGR=TIM_EGR_UG;') < line.index('TIM4->SMCR=TIM_SMCR_TS_1')
assert line.index('DMA1_Channel2->CCR=0u;') < line.index('DMA1_Channel2->CPAR=') < line.index('DMA_CCR_EN;') < line.index('TIM3->CR1=TIM_CR1_CEN;')
assert 'TIM3->DIER=TIM_DIER_CC3DE;' in line and 'DMA1_Channel2->CPAR=(uint32_t)(uintptr_t)&GPIOG->IDR;' in line
assert 'DMA_CCR_PSIZE_1|DMA_CCR_MSIZE_0|DMA_CCR_MINC' in line
assert all(x not in line for x in ['DMA_CCR_PINC|','DMA_CCR_MEM2MEM|','DMA_CCR_CIRC|','DMA_CCR_DIR|'])
assert line.index('uint8_t status=c3_fd_check') < line.rindex('TIM3->CR1=0u;')
assert '#if C3_FIFO_DMA_PROBE\n    c3_fifo_dma_probe_run(camera_ok);' in main
assert main.index('c3_fifo_dma_probe_run(camera_ok);') < main.index('MX_SDIO_SD_Init();',main.index('c3_fifo_dma_probe_run(camera_ok);'))
assert 'c3_camera_bmp_fast_init() && c3_camera_capture(1600u) && c3_camera_raw_hold()' in src
assert src.count('c3_camera_raw_rewind()')==2 and 'c3_camera_raw_discard()' in src
assert 'row_crc[y]=c;' in src and 'c!=row_crc[y] || exact' in src and 'TEST_REPEATS 2u' in src
assert 'post_ok=(bad==0u && post_crc==baseline_crc);' in src
assert not any(s in src for s in ['f_write(', 'f_mount(', 'c3_storage_', 'HAL_FLASH_', 'c3_settings_flash_save('])

def ihex(path):
    base=0; data={}
    for l in path.read_text().splitlines():
        b=bytes.fromhex(l[1:]);assert l[0]==':' and len(b)==b[0]+5 and sum(b)%256==0
        n,a,ty=b[0],int.from_bytes(b[1:3],'big'),b[3]
        if ty==4:base=int.from_bytes(b[4:6],'big')<<16
        elif ty==0:
            for i,v in enumerate(b[4:4+n]): data[base+a+i]=v
        elif ty==1:break
        elif ty==5:pass
        else:raise AssertionError(ty)
    assert min(data)==0x08000000
    return bytes(data[x] for x in range(min(data),max(data)+1))

for build in ['DMA_PROBE','REF']:
    out=root/f'build_V194_{build}_ACR8_F2'
    if not out.exists():continue
    sym=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
    if build=='DMA_PROBE':
        assert 'c3_fifo_dma_probe_run' in sym
        for bad in ['f_mount','f_write','c3_storage_mount','HAL_FLASH_Program','HAL_FLASHEx_Erase','c3_settings_flash_save']:
            assert bad not in sym,bad
    else:assert 'c3_fifo_dma_probe_run' not in sym and 'exact_ref' not in sym
    data=ihex(out/'c3m1.hex'); assert data==(out/'c3m1.bin').read_bytes()
    assert struct.unpack_from('<I',data)[0]==0x20010000
    assert 0x08000000<=struct.unpack_from('<I',data,4)[0]<0x08000000+len(data)
    print(f'PASS: {build} linked symbol isolation, vector, HEX/BIN {len(data)} bytes, SHA256 {hashlib.sha256(data).hexdigest()}')
print('PASS: timer gate, rearm ordering, capture/rewind and read-only source contract')

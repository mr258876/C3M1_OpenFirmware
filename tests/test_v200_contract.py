from pathlib import Path
import hashlib,struct,subprocess,tempfile
root=Path(__file__).resolve().parent.parent
s=(root/'C3M1_Camera/Core/Src/c3_fifo_dma_live.inc').read_text()
c=(root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
p=s[s.index('static void live_fifo_prepare'):s.index('static uint32_t live_fifo_go')]
assert p.index('TIM4->SMCR=0u;')<p.index('TIM3->EGR=TIM_EGR_UG;')<p.index('TIM4->SMCR=TIM_SMCR_TS_1')
assert 'DMA1_Channel2->CNDTR=C3_LIVE_BYTES;' in p and 'TIM4->CCR1=C3_LIVE_BYTES;' in s
assert 'DMA_CCR_PSIZE_1|DMA_CCR_MSIZE_0|DMA_CCR_MINC' in p
assert 'GPIOG->IDR' in p
assert 'static const c3_fd_plan_t live_plan={0,C3_FIFO_DMA_LIVE_TICKS,C3_FIFO_DMA_LIVE_TICKS-2u,1u};' in s
finish=s[s.index('static uint8_t live_fifo_finish'):s.index('static void live_lcd_start')]
assert finish.index('while((uint32_t)(DWT->CYCCNT-start)')<finish.index('unsigned cnt=TIM3->CNT;')<finish.index('c3_live_check(')<finish.index('TIM3->CR1=0u;')
loop=s[s.index('static uint8_t live_draw'):s.index('static void live_error_hud')]
for a,b in zip(['if(next)live_fifo_prepare(b+1u);','if(prev)live_lcd_start(b-1u);','if(next)t=live_fifo_go();','live_render_block(b,p','if(next){status=live_fifo_finish(t);'],['if(prev)live_lcd_start(b-1u);','if(next)t=live_fifo_go();','live_render_block(b,p','if(next){status=live_fifo_finish(t);','uint8_t lcd=live_lcd_finish();']):
 assert loop.index(a)<loop.index(b)
assert 'live_session_end();' in loop and 'for(unsigned b=0;!status&&b<C3_LIVE_BLOCKS;++b)' in loop
assert 'live_disabled=1u;' in c and 'c3_camera_preview_rearm();\n            live_error_hud();\n            return;' in c
assert c.index('#include "c3_fifo_dma_live.inc"')<c.index('void c3_camera_preview_draw(')
for variant in ('SP224','SP256','SP224_PROFILE','SP256_PROFILE','GREEN_REF'):
 out=root/f'build_V200_{variant}_ACR8_F2'
 data={};base=0
 for l in (out/'c3m1.hex').read_text().splitlines():
  b=bytes.fromhex(l[1:]);assert l[0]==':' and len(b)==b[0]+5 and sum(b)%256==0
  n,a,typ=b[0],int.from_bytes(b[1:3],'big'),b[3]
  if typ==4:base=int.from_bytes(b[4:6],'big')<<16
  elif typ==0:
   for i,v in enumerate(b[4:4+n]):data[base+a+i]=v
  elif typ==1:break
  elif typ==5:pass
  else:raise AssertionError(typ)
 raw=bytes(data[x] for x in range(min(data),max(data)+1))
 assert min(data)==0x08000000 and raw==(out/'c3m1.bin').read_bytes()
 assert struct.unpack_from('<I',raw)[0]==0x20010000 and 0x08000000<=struct.unpack_from('<I',raw,4)[0]<0x08000000+len(raw)
 with tempfile.TemporaryDirectory() as d:
  elfbin=Path(d)/'fromelf.bin'
  subprocess.run(['llvm-objcopy','-O','binary',str(out/'c3m1.elf'),str(elfbin)],check=True)
  assert raw==elfbin.read_bytes()
 sym=subprocess.check_output(['readelf','-sW',str(out/'c3m1.elf')],text=True)
 for name in ('live_raw','live_out','c3_live_errors','c3_live_last_error','f_write','c3_camera_capture'):
  assert name in sym,name
 if variant.endswith('_PROFILE'):assert 'c3_live_profile' in sym
 print(f'PASS {variant}: {len(raw)} bytes, full capture/storage linked; HEX/BIN SHA256 {hashlib.sha256(raw).hexdigest()}')
assert 'lp.wait+=c3_live_slack(elapsed,budget);' in loop and 'if(elapsed>budget)lp.over++;' in loop
print('PASS live DMA source contract: gate reset order, DMA widths, no active polling in block control, ownership/finish order, rearm on error')

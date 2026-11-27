"""Host exercise of the exact camera wait body, with mock IRQ/time/VSYNC.
No physical timing or hardware claim."""
from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1]; src=(root/'C3M1_Camera/Core/Src/c3_camera.c').read_text()
a=src.index('uint8_t c3_camera_wait_zoom_frame(void)'); b=src.index('void c3_camera_preview_rearm(void)',a)
body=src[a:b]
head=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define C3_SCHED_OFF 0
#define OV7725_VSYNC_Pin 8
static uint8_t raw_frame_held;
static struct { unsigned mode; } frame_sched;
static volatile uint32_t c3_camera_vsync_edges;
static uint32_t now,elapsed,e1,e2,cleared,locked;
static uint32_t irq_lock(void){assert(!locked);locked=1;return 0;}
static void irq_unlock(uint32_t p){assert(!p&&locked);locked=0;}
static void clear(unsigned p){assert(p==8&&locked);cleared++;}
#define __HAL_GPIO_EXTI_CLEAR_IT(p) clear(p)
static uint32_t HAL_GetTick(void){return now;}
static void HAL_Delay(uint32_t d){assert(d==1&&!locked);now+=d;elapsed+=d;if(elapsed==e1||elapsed==e2)c3_camera_vsync_edges++;}
'''
tail=r'''
int main(void){
 unsigned n=0;
 for(unsigned i=1;i<=1000;i+=37)for(unsigned gap=1;gap<=1000;gap+=43){
  now=UINT32_MAX-10;elapsed=cleared=0;e1=i;e2=i+gap;c3_camera_vsync_edges=UINT32_MAX;
  assert(c3_camera_wait_zoom_frame());assert(elapsed==e2&&cleared==1&&!locked);n++;
 }
 elapsed=cleared=0;e1=50;e2=0;assert(!c3_camera_wait_zoom_frame()&&elapsed==2200&&cleared==1);
 elapsed=cleared=0;e1=e2=0;assert(!c3_camera_wait_zoom_frame()&&elapsed==2200);
 raw_frame_held=1;elapsed=cleared=0;assert(!c3_camera_wait_zoom_frame()&&!elapsed&&!cleared);raw_frame_held=0;
 frame_sched.mode=1;assert(!c3_camera_wait_zoom_frame()&&!elapsed&&!cleared);
 printf("PASS %u two-edge barrier combinations including tick/edge wrap, timeout and active FIFO guards\n",n);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'wait.c';p.write_text(head+body+tail);exe=p.with_suffix('')
 subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)

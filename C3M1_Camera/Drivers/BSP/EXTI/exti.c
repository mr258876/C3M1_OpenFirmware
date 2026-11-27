/* C3M1 production-board VSYNC EXTI hook.
 * HAL's EXTI9_5 IRQ checks/clears the pending flag; this callback performs
 * only the bounded frame-scheduler transition and PB3 FIFO write gating.
 */
#include "../../SYSTEM/sys/sys.h"
#include "exti.h"
#include "c3_camera.h"

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == OV7725_VSYNC_Pin) c3_camera_vsync_isr();
}

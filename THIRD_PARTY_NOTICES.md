# Third-party notices

| Component | Location | Terms / attribution |
| --- | --- | --- |
| STM32F1 HAL and low-level drivers | `C3M1_Camera/Drivers/STM32F1xx_HAL_Driver` | STMicroelectronics; bundled `LICENSE.txt` specifies BSD-3-Clause when supplied outside the original package. |
| CMSIS core | `C3M1_Camera/Drivers/CMSIS` | Arm; Apache License 2.0, bundled `LICENSE.txt`. |
| STM32F1 device support and startup | `C3M1_Camera/Drivers/CMSIS/Device/ST/STM32F1xx`, `Core/Startup` | STMicroelectronics; retain original file notices and device license. |
| FatFs R0.11 | `C3M1_Camera/Middlewares/Third_Party/FatFs` | ChaN, 2015; redistribution terms in `ff.c`, with original integration notices retained. |
| Chinese native 16px bitmap glyph subset | `C3M1_Camera/Core/Src/c3_i18n_data.inc` | Arphic Technology and Arne Goetje; [Arphic Public License](docs/ARPHICPL.TXT), [source and modification notice](docs/C3_UI_bitmap_UMing.txt). |
| Camera and platform driver code inherited from original camera project | `C3M1_Camera/Drivers/BSP`, `Drivers/SYSTEM`, associated sensor register tables | Original file-level notices, including ALIENTEK / Guangzhou Xingyi Electronic Technology Co., Ltd. |

Before public distribution, confirm the redistribution terms of the inherited camera/platform source and select a project license for C3M1-authored code. Keep the component licenses and attribution when distributing firmware or source.

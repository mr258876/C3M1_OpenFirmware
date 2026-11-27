# C3M1 OpenFirmware

[English](README.md) · [变更清单](CHANGELOG.md) · [开发说明](AGENTS.md)

C3M1 OpenFirmware 是面向 C3M1 数码相机的替代固件，在原硬件上实现直接曝光控制、RAW DNG 拍摄、辅助对焦、照片回看和中英文界面。当前版本为 **v0.2.21**。

[GitHub 项目地址](https://github.com/mr258876/C3M1_OpenFirmware) · [AGPL-3.0](LICENSE) · Copyright © 2026 [mr258876](https://github.com/mr258876)

## 功能

- P/S/M 曝光模式；九档手动快门（1/50～1/8000 秒）、AUTO 与 1×～16× 增益、±3 EV 补偿。
- 642×480 RAW8 BGGR DNG，记录拍摄时白平衡与标定黑电平，内嵌 C3M1 Soft M675 色彩配置；支持 320×240、24-bit BMP。
- 全局稀疏峰值对焦、共享梯度评分、4X 对焦放大与曝光/白平衡锁定、网格、黑白取景和可选 FPS 显示。
- 照片回看、即时回放、拍后过片手柄、保存失败重试与共用照片编号。
- 中英文点阵菜单、实体按键图标、开机提示、估算电量、自动休眠、恢复默认设置及存储卡格式化。

## 硬件与构建

STM32F103ZET6（72 MHz、512 KiB Flash、64 KiB SRAM）、OV7725、AL422B（384 KiB FIFO）、ST7789V3 320×240 LCD、SDIO 存储卡。

取景使用 5.14 MHz 四行 FIFO DMA、CPU 合成与 LCD DMA 流水线；v0.2.03 的实机测量中，峰值对焦和评分同时开启时达到 27.3 FPS。拍摄保留 CPU FIFO 读取，SDIO 使用 DMA 写入。

安装 Bash、Clang 17、LLVM LLD 和 `llvm-objcopy` 后，在项目根目录执行：

```sh
git clone https://github.com/mr258876/C3M1_OpenFirmware.git
cd C3M1_OpenFirmware
bash build.sh
```

输出为 `build/c3m1.hex`、`build/c3m1.bin`、`build/c3m1.elf` 和 `build/c3m1.map`。普通构建只使用仓库内的源码、点阵和标定表，不需要 CubeMX、Keil 或额外字体。

## 刷机

通过 SWD 连接 GND、SWDIO、SWCLK 和正确的目标电压参考，先备份整片 512 KiB 原始 Flash。写入并校验 HEX，重启相机；BIN 的烧录起始地址是 `0x08000000`。

设置保存在最后两个 2 KiB Flash 页（`0x0807F000`、`0x0807F800`）。保留设置时选择应用区页擦除；首次初始化或恢复整片原始备份时按需要处理设置页。

## 操作方式

| 按键 | 取景时的功能 |
| --- | --- |
| 快门旋钮 | 选择快门；其余档位为 AUTO |
| ISO 短按 | 手动快门下循环 AUTO、1X、2X、4X、8X、16X |
| ISO 长按 | 开关峰值对焦 |
| 4X | 开关对焦放大，AE-L 表示锁定 |
| 快门 | 拍摄 |
| 过片扳手长按 | 打开功能菜单 |
| 过片扳手拨动 | 开启拍后过片时，推进下一张拍摄 |

AUTO 快门＋AUTO ISO 为 P 档；手动快门＋AUTO ISO 为 S 档；两者手动为 M 档。P 档短按 ISO 不改变增益；旋钮拨回 AUTO 时，增益恢复 AUTO。

**功能菜单：** 转动旋钮调整 EV，4X 进入设置，ISO 进入回看，快门返回。右上角显示电压与估计电量。退出时留意实体快门旋钮的位置。

**设置：** 旋钮选择，4X 设置或打开二级页，快门返回。首次开机或重置设置时必须先选择语言，按4X确认保存。

**回看：** 旋钮切换照片，ISO 显示或隐藏信息，快门恢复取景。DNG 使用文件内的元数据生成快速 Bayer 预览。

## 默认设置

峰值对焦开、红色、高灵敏度；对焦评分开；帧率显示、网格、黑白取景关；参数显示开；保存格式 DNG；屏幕旋转180°开；自动休眠1分钟；开机提示、即时回放、拍后过片均开。语言在首次开机选择。

| 即时回放 | 拍后过片 | 保存后的行为 |
| --- | --- | --- |
| 关 | 关 | 直接恢复取景 |
| 开 | 关 | 显示照片5秒，任意键提前退出 |
| 关 | 开 | 保留最后一帧取景，显示保存成功及过片提示 |
| 开 | 开 | 显示照片和过片提示，立即等待过片 |

拍后过片开启时，仅新的手柄触发能够解除等待。重置所有设置不会删除照片。

## 存储

BMP 与 DNG 共用 `C3_#####` 编号；失败保存可从被冻结的 FIFO 重试，不重新曝光。设置中的格式化功能支持不超过32 GiB的 FAT 存储卡，确认前先备份卡内文件。

源码结构、编译配置与硬件约束见 [AGENTS.md](AGENTS.md)。第三方依赖及许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 许可证

C3M1 OpenFirmware 项目代码采用 GNU Affero General Public License 第3版（AGPL-3.0-only），Copyright © 2026 mr258876。完整条款见 [LICENSE](LICENSE)，随项目提供的第三方组件保留各自的授权条款及版权声明，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

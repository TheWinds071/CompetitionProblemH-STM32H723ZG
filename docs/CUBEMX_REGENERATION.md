# CubeMX 重新生成说明

## 已写入 H723ZG.ioc 的配置

- SPI2：单线主机发送、8-bit、Mode 0、31.25 Mbit/s；
- SPI2 TX DMA：DMA1 Stream 0、Memory-to-Peripheral、Normal、High priority；
- DMA1 Stream 0 中断：抢占优先级 5；
- SPI2 中断：抢占优先级 5；
- PB13/PB15：Very High GPIO speed；
- PD10、PD11、PD14：GPIO input、Pull-up，按键按下为低电平。

这些配置会重新生成 `dma.c`、`spi.c`、`gpio.c` 和对应中断代码。

## 不由 CubeMX 管理的代码

以下代码位于独立目录，CubeMX 重新生成时不会删除：

- `Drivers/ST7789`；
- `App/Inc/task_ui.h`；
- `App/Src/task_ui.c`；
- 根目录 `CMakeLists.txt` 中的自定义源文件和头文件路径。

`main.c` 中的 UI、任务调度和驱动初始化均放在 `USER CODE` 区域，会由
CubeMX 的 `Keep User Code` 功能保留。

应用使用 `linker/STM32H723xG_app.ld`，其中包含 DMA 可访问的 AXI SRAM
缓冲区段。该文件不属于 CubeMX 输出，即使 CubeMX 重建默认链接脚本，
根目录 CMake 仍会选择应用链接脚本。

## 看到旧配置时

CubeMX 已打开项目时不会自动合并外部修改。不要在旧窗口中直接保存：

1. 关闭当前 CubeMX 项目；若提示保存旧配置，选择不保存；
2. 从磁盘重新打开 `H723ZG.ioc`；
3. 在 SPI2、DMA、NVIC 和 GPIO 页面确认上述配置；
4. 再执行 Generate Code。

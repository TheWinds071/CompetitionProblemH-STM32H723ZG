# 1.47 寸 ST7789 LCD DMA 驱动说明

## 硬件假设

本驱动按 Waveshare 1.47 寸 LCD 模组实现：

- 控制器：ST7789V3；
- 可见分辨率：172 × 320；
- 接口：4 线 SPI；
- 像素格式：RGB565；
- 竖屏模式，列地址偏移 34 像素。

参考资料：

- [Waveshare 1.47inch LCD Module](https://www.waveshare.com/wiki/1.47inch_LCD_Module)
- [ST7789V3 数据手册](https://files.waveshare.com/upload/c/c1/ST7789V3_V0.1.pdf)

如果实际屏幕不是上述模组，需要调整 `st7789.c` 中的初始化序列、分辨率、方向和地址偏移。

## STM32 接线

| LCD | STM32H723 | 工程名称 |
|---|---|---|
| CLK | PB13 / SPI2_SCK | `LCD_SCK` |
| DIN | PB15 / SPI2_MOSI | `LCD_MOSI` |
| DC | PB14 | `LCD_D_C` |
| RST | PD8 | `LCD_RESET` |
| CS | PD9 | `LCD_CS` |
| GND | GND | 必须共地 |

工程中没有 LCD 背光 GPIO。如果模组带 `BL` 引脚，需要将其接到合适的电源或另行配置 PWM。

## CubeMX 配置

`H723ZG.ioc` 中已经加入以下配置：

- SPI2 Master；
- 单线发送模式；
- 8-bit Data Size；
- SPI Mode 0；
- SPI2 时钟约 31.25 Mbit/s；
- SPI2_TX → DMA1 Stream0；
- DMA Memory-to-Peripheral；
- Memory Increment；
- Byte 对齐；
- Normal 模式；
- DMA1 Stream0 和 SPI2 中断优先级 5。

SPI2 中断不能省略。STM32H7 HAL 在 DMA 传输完毕后通过 SPI EOT 中断完成状态切换并调用 `HAL_SPI_TxCpltCallback()`。

## DMA 内存

DMA1 无法访问 DTCM，而本工程默认把 `.data` 和 `.bss` 放在 DTCM。因此链接脚本增加了 `.dma_buffer` 段，将 LCD 的 352 字节对齐行缓冲放在 `RAM_D1`：

```text
dma_row_buffer = 0x24000000
```

驱动按行转换 CPU 端 RGB565 数据为 LCD 所需的大端字节流，然后连续启动 DMA。无需分配完整的 110080 字节帧缓冲。

如果以后开启 Cortex-M7 D-Cache，驱动会在每次 DMA 启动前自动清理行缓冲对应的 Cache。

## 初始化及启动测试

`main.c` 在开启 5 V/12 V 电源后初始化 LCD，并使用 DMA 显示红、绿、蓝三个横向色块。三个颜色正常显示表示以下部分均能工作：

- SPI2 时钟和 MOSI；
- CS、DC、RESET；
- ST7789 初始化序列；
- DMA1 Stream0；
- DMA 和 SPI2 EOT 中断；
- 172 × 320 地址偏移。

后续加入正式界面时，可以删除 `main.c` 中的三段 `ST7789_FillRect_DMA()` 测试代码。

## API

### 初始化

```c
ST7789_Init(&hspi2);
```

### DMA 清屏

```c
ST7789_FillScreen_DMA(ST7789_COLOR_BLACK);
ST7789_Wait(1000U);
```

### DMA 填充矩形

```c
ST7789_FillRect_DMA(10U, 20U, 80U, 40U, ST7789_COLOR_BLUE);
ST7789_Wait(1000U);
```

### DMA 绘制 RGB565 图像

```c
ST7789_DrawRGB565_DMA(x, y, width, height, pixels);
ST7789_Wait(1000U);
```

`pixels` 是 CPU 字节序的 `uint16_t` RGB565 数组，必须连续存放，并且在 DMA 完成前保持有效且不能修改。

### 非阻塞使用

所有绘制函数只负责启动 DMA。可以通过以下函数查询：

```c
if (ST7789_IsBusy() == 0U)
{
  /* 上一次刷新已经完成 */
}
```

同一时刻只能有一个 LCD DMA 任务。忙碌期间再次调用绘制函数会返回 `HAL_BUSY`。

## CubeMX 重新生成

SPI2、DMA1 Stream 0、NVIC 和 GPIO 电气参数均保存在 `H723ZG.ioc` 中，
可由 CubeMX 重新生成。LCD 驱动和 UI 位于独立目录，不属于 CubeMX 生成文件。

DMA 帧缓冲所需的 `.dma_buffer` 段保存在
`linker/STM32H723xG_app.ld`。根目录 `CMakeLists.txt` 固定使用该应用链接脚本，
因此 CubeMX 以后重新生成默认的 `STM32H723xG_flash.ld` 不会覆盖 DMA 内存布局。

## RGB565

可以使用宏生成颜色：

```c
uint16_t orange = ST7789_RGB565(255U, 128U, 0U);
```

驱动也提供黑、白、红、绿、蓝常量。

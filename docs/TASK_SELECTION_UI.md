# 六任务选择 UI

## 操作方式

屏幕显示 `TASK 1` 到 `TASK 6`，橙色条目表示当前选择。

| 按键 | 引脚 | 动作 |
| --- | --- | --- |
| Button1 | PD10 | 上一个任务 |
| Button2 | PD11 | 下一个任务 |
| Button3 | PD14 | 确认并启动 |

三个按键均为低电平有效，GPIO 使用内部上拉，并进行 30 ms 软件消抖。
在第一个和最后一个任务之间可以循环切换。

## 任务入口

任务入口位于 `Core/Src/main.c`：

- `App_StartTask1()` 已连接 `LineFollower_Start()`；
- `App_StartTask2()` 到 `App_StartTask6()` 是预留入口；
- 每次确认新任务前都会先调用 `LineFollower_Stop()`，避免切换任务时电机继续执行旧任务。

增加任务代码时，将对应的 `App_StartTaskN()` 函数体替换为实际启动逻辑即可。

## 显示实现

`App/Src/task_ui.c` 使用 172 × 320 RGB565 帧缓冲区绘制界面，再通过
ST7789 的 SPI2 TX DMA 接口整屏刷新。帧缓冲区放在 DMA1 可访问的 AXI SRAM，
不会占用 DTCM。

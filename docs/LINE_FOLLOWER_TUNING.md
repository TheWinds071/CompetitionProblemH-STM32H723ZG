# TASK1 巡线速度调节说明

## 1. 参数位置与单位

TASK1 调用 `LineFollower_Start()` 启动巡线。速度和停车标志参数位于
`Drivers/DRV8870/Inc/line_follower.h`。

速度参数的单位不是 PWM 百分比，而是每个控制周期的编码器计数。当前控制周期由
`LINE_FOLLOW_CONTROL_PERIOD_MS` 定义为 1 ms，因此：

```text
编码器计数/秒 = 速度参数 × 1000
```

实际线速度还与轮径和每圈编码器计数有关：

```text
线速度 = 速度参数 × 1000 × 车轮周长 / 每圈编码器计数
```

## 2. 主要速度参数

| 参数 | 作用 | 调节原则 |
| --- | --- | --- |
| `LINE_FOLLOW_BASE_SPEED_TICKS` | 直线巡航目标速度 | 提速时优先增加 |
| `LINE_FOLLOW_MAX_SPEED_TICKS` | 单轮目标速度上限 | 必须高于基础速度，并为转向留出余量 |
| `LINE_FOLLOW_MAX_STEERING_TICKS` | 转向时左右轮最大速度修正量 | 高速无法入弯时适当增加 |
| `LINE_FOLLOW_TEST_PWM_LIMIT` | 速度 PID 的最大 PWM 输出 | 实际范围为 0～1000，逐步增加 |
| `LINE_FOLLOW_LOST_SEARCH_TICKS` | 丢线后的原地搜索速度 | 通常保持低速，不随巡航速度同比增加 |

为了避免弯道外侧轮过早达到速度上限，建议满足：

```text
LINE_FOLLOW_MAX_SPEED_TICKS
    >= LINE_FOLLOW_BASE_SPEED_TICKS
       + LINE_FOLLOW_MAX_STEERING_TICKS
```

例如基础速度为 `0.60F`、最大转向修正为 `0.25F` 时，最大速度至少应为
`0.85F`，建议设置为 `0.90F` 以保留少量余量。

## 3. 推荐提速档位

以下数值用于逐级实车测试，不应直接从低速跳到最高档：

| 档位 | 基础速度 | 最大速度 | 最大转向修正 | PWM 上限 |
| --- | ---: | ---: | ---: | ---: |
| 低速验证 | `0.50F` | `0.70F` | `0.20F` | `300.0F` |
| 中速 | `0.60F` | `0.90F` | `0.25F` | `400.0F` |
| 较高速 | `0.75F` | `1.10F` | `0.30F` | `500.0F` |
| 高速试验 | `0.90F` | `1.30F` | `0.35F` | `600.0F` |

每次只提升一个档位。第一次使用新档位时先架空驱动轮，确认两个编码器方向、
左右轮转向和制动逻辑正确，再落地测试。

## 4. 调试观察量

通过 `LineFollower_GetState()` 返回的状态观察：

| 字段 | 含义 |
| --- | --- |
| `left_target_speed` / `right_target_speed` | 速度环的左右轮目标 |
| `left_encoder_delta` / `right_encoder_delta` | 当前 1 ms 内的编码器计数 |
| `left_pwm` / `right_pwm` | 速度 PID 最终输出 |
| `line_position` | 当前黑线位置误差 |
| `gray_active_mask` / `gray_active_count` | 检测到黑线的传感器和数量 |
| `stop_marker_cycles` | 三路以上黑线连续出现的帧数 |

根据现象调整：

- PWM 长时间达到上限，但编码器速度仍低于目标：逐步提高
  `LINE_FOLLOW_TEST_PWM_LIMIT`，并检查电池、电机负载和机械阻力。
- 实际速度已经跟随目标，但直线速度仍低：同时提高基础速度和最大速度。
- 高速时转不过弯：提高 `LINE_FOLLOW_MAX_STEERING_TICKS`；若修正仍不足，再调整转向
  Kp。
- 高速时左右摆动：先降低转向 Kp；由于灰度位置是离散信号，Kd 过大也可能放大跳变。
- 左右轮速度剧烈波动：再调整速度 Kp/Ki，不要把速度 PID 作为第一项提速参数。

## 5. PID 与 EEPROM

`LINE_FOLLOW_STEERING_*` 和 `LINE_FOLLOW_SPEED_*` 是 PID 默认值。系统启动时会优先
从 EEPROM 加载已保存参数，因此只修改头文件中的 PID 默认值不一定立即生效。

可以通过 ESP32 协议：

- 使用 `TURN` 或 `SPEED` 命令直接调节并保存 PID；
- 修改默认值并重新烧录后，发送 `DEFAULT`，将新的默认 PID 写入 EEPROM；
- 使用 `GET` 检查当前实际运行参数。

基础速度、速度上限、最大转向修正和 PWM 上限不是 EEPROM 参数，修改后需要重新编译
并烧录 STM32 固件。

## 6. 停车标志

当前三路灰度传感器从左到右为 `L3、L2、L1`，其中 `L2` 位于中间。TASK1 仅在
`L3 + L2 + L1` 三路全部同时检测到黑线时识别停车标志。

当前控制周期为 1 ms，`LINE_FOLLOW_STOP_MARKER_CYCLES` 为 5，因此连续 5 帧约为
5 ms。任意一帧不满足三路同时为黑线都会清零计数；达到阈值后，左右电机立即
进入 DRV8870 制动状态。

高速时停车距离主要由车速、轮胎附着力和车辆惯性决定。减小确认帧数只能缩短识别延迟，
不能消除机械制动距离。

## 7. 调节顺序

建议按以下顺序调节：

1. 保持 1 ms 控制周期不变。
2. 提高基础速度。
3. 按参数关系同步提高最大速度。
4. PWM 饱和且实际速度不足时，再提高 PWM 上限。
5. 根据弯道表现调整最大转向修正。
6. 最后根据振荡、跟随误差调整转向 PID 和速度 PID。

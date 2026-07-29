# 循线 PID、AT24CS32 与 ESP32 通信说明

## 1. 功能概览

系统启动时从 AT24CS32 EEPROM 读取循线 PID 参数。EEPROM 中没有有效记录、记录 CRC 错误或参数越界时，程序使用 `line_follower.h` 中的默认参数。ESP32 可通过 UART4 查询、修改、保存、重新加载或恢复默认 PID 参数。

控制器包含两组参数：

- `STEERING`：灰度位置/转向外环 PID；
- `SPEED`：左右轮共用的速度内环 PID。

ESP32 修改 PID 时，新参数会同时应用到控制器并保存到 EEPROM。EEPROM 写入或回读校验失败时，控制器回滚到修改前的参数。

## 2. 硬件连接

### AT24CS32-STUM

AT24CS32-STUM 是 5 引脚 SOT23 封装，没有外部 A0/A1/A2 引脚，EEPROM 区域的 7 位 I2C 地址固定为 `0x50`。

| AT24CS32 | STM32H723 | 说明 |
|---|---|---|
| SCL | PB8 / I2C1_SCL | 需要上拉电阻 |
| SDA | PB9 / I2C1_SDA | 需要上拉电阻 |
| WP | GND | 拉低才能写 EEPROM |
| VCC | 3.3 V | 电源附近放置去耦电容 |
| GND | GND | 两端共地 |

SDA 和 SCL 是开漏信号，应使用外部上拉电阻。常见起始值为 4.7 kΩ，最终值应根据总线电容和速率确认。WP 如果接到 VCC，读取仍然正常，但所有 PID 保存操作都会失败回读校验。

器件容量为 4096 字节，页大小为 32 字节。驱动自动按页边界拆分写操作，并在每页写入后执行 ACK 轮询。器件规格可参考 [Microchip AT24CS32 数据手册](https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20006341A.pdf)。

### ESP32 UART

| STM32H723 | ESP32 | 参数 |
|---|---|---|
| PC10 / UART4_TX | ESP32 RX | 115200 baud |
| PC11 / UART4_RX | ESP32 TX | 8 数据位、无校验、1 停止位 |
| GND | GND | 必须共地 |

两端均使用 3.3 V UART 电平，不要接 RS-232 电平。

## 3. EEPROM 数据格式

为了降低掉电写入导致参数全部丢失的风险，PID 参数使用两个槽轮换保存：

| 地址 | 内容 |
|---|---|
| `0x0000` | 槽 0，40 字节记录 |
| `0x0040` | 槽 1，40 字节记录 |

每条记录包含：

- 32 位魔数 `0x50494443`；
- 16 位格式版本；
- 16 位记录长度；
- 32 位递增序号；
- 转向 PID 的 Kp、Ki、Kd；
- 速度 PID 的 Kp、Ki、Kd；
- 32 位 CRC32。

加载时会校验魔数、版本、长度和 CRC，并从有效槽中选择序号较新的记录。保存时写入较旧或无效的槽，随后回读并逐字节校验。EEPROM 的 `0x0000` 至 `0x0067` 区域由 PID 存储模块保留。

## 4. UART 文本协议

协议使用 ASCII 文本，一条消息以 `\n` 结束，也接受 `\r\n`。字段使用英文逗号分隔，命令区分大小写。每条命令最长 191 字节。

### 连通测试

请求：

```text
PING
```

应答：

```text
OK,PONG
```

### 查询当前参数

请求：

```text
PID,GET
```

应答字段依次为转向 Kp、Ki、Kd、速度 Kp、Ki、Kd：

```text
OK,PID,ALL,0.000700,0.000000,0.000015,220.000000,800.000000,0.000000
```

### 修改转向 PID

```text
PID,SET,STEERING,0.0008,0,0.00002
```

### 修改速度 PID

```text
PID,SET,SPEED,230,850,0
```

### 一次修改全部 PID

参数顺序为转向 Kp、Ki、Kd、速度 Kp、Ki、Kd：

```text
PID,SET,ALL,0.0008,0,0.00002,230,850,0
```

修改成功后返回完整的当前参数。`SET` 命令会立即保存 EEPROM，因此不要以高频率连续发送相同参数，以免无谓消耗 EEPROM 写入寿命。

### 从 EEPROM 重新加载

```text
PID,LOAD
```

### 恢复并保存编译默认值

```text
PID,DEFAULT
```

## 5. 参数范围

为防止串口异常数据造成极端控制输出，固件限制了参数范围：

| 参数 | 最小值 | 最大值 |
|---|---:|---:|
| 转向 Kp | 0 | 0.1 |
| 转向 Ki | 0 | 1.0 |
| 转向 Kd | 0 | 0.1 |
| 速度 Kp | 0 | 2000 |
| 速度 Ki | 0 | 10000 |
| 速度 Kd | 0 | 100 |

支持普通小数和科学计数法；不接受负数、`NaN`、无穷大或包含多余字符的数值。

## 6. 错误应答

| 应答 | 含义 |
|---|---|
| `ERR,BAD_COMMAND` | 未识别命令或字段 |
| `ERR,BAD_ARGUMENT` | 参数数量或数值格式错误 |
| `ERR,PID_RANGE` | PID 参数超出允许范围 |
| `ERR,EEPROM` | EEPROM 不在线、记录无效、写入或校验失败 |
| `ERR,RX_OVERFLOW` | UART 接收环形缓冲区溢出 |
| `ERR,LINE_TOO_LONG` | 当前命令超过长度限制 |
| `ERR,NOT_READY` | 循线控制器尚未初始化 |

收到行长度或接收溢出错误后，应等待错误应答，再重新发送完整命令。

## 7. ESP32 Arduino 示例

下面的 GPIO 编号仅为示例，应替换为实际连接的 ESP32 引脚：

```cpp
HardwareSerial stm32(2);

void setup()
{
  Serial.begin(115200);
  stm32.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  stm32.print("PID,GET\n");
}

void loop()
{
  if (stm32.available())
  {
    String response = stm32.readStringUntil('\n');
    Serial.println(response);
  }
}
```

## 8. 相关源码

- `Drivers/AT24CS32/`：EEPROM 页读写和 ACK 轮询；
- `Drivers/DRV8870/Src/pid_storage.c`：PID 双槽记录和 CRC32；
- `Drivers/DRV8870/Src/line_follower.c`：运行时 PID 获取、校验和更新；
- `Drivers/ESP32_PID/`：UART4 接收、命令解析和应答；
- `Core/Src/main.c`：启动加载和主循环协议处理。

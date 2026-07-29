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

## 4. UART 帧协议

每个请求和应答都使用相同帧格式：

| 区域 | 字节 | 说明 |
|---|---|---|
| 包头 | `AA 55` | 两个固定 HEX 字节 |
| 正文 | UTF-8 | 命令、参数或应答，不包含换行 |
| 包尾 | `55 AA` | 两个固定 HEX 字节 |

完整结构为：

```text
AA 55 + UTF-8 正文 + 55 AA
```

正文最长 191 字节，命令区分大小写，数值和字段使用英文逗号分隔。当前命令只使用 UTF-8 的 ASCII 子集，因此 ESP32 可以直接用字符串生成正文。不要在正文后添加 `\r` 或 `\n`。

例如 `PING` 正文编码后的完整串口数据为：

```text
AA 55 50 49 4E 47 55 AA
```

### 命令正文

| 功能 | UTF-8 正文 |
|---|---|
| 连通测试 | `PING` |
| 查询当前 PID | `GET` |
| 修改转向 PID | `TURN,Kp,Ki,Kd` |
| 修改速度 PID | `SPEED,Kp,Ki,Kd` |
| 修改全部 PID | `ALL,转向Kp,转向Ki,转向Kd,速度Kp,速度Ki,速度Kd` |
| 从 EEPROM 重新加载 | `LOAD` |
| 恢复并保存默认值 | `DEFAULT` |

示例：

```text
TURN,0.0008,0,0.00002
SPEED,230,850,0
ALL,0.0008,0,0.00002,230,850,0
```

上述每段文本都只是正文，发送时必须在前后加上包头和包尾。例如查询 PID 的完整 HEX 数据为：

```text
AA 55 47 45 54 55 AA
```

### 正常应答正文

| 请求 | 应答正文 |
|---|---|
| `PING` | `PONG` |
| `GET` | `PID,转向Kp,转向Ki,转向Kd,速度Kp,速度Ki,速度Kd` |
| 参数修改成功 | 返回修改后的 `PID,...` |
| `LOAD` 成功 | 返回加载后的 `PID,...` |
| `DEFAULT` 成功 | 返回默认的 `PID,...` |

查询默认参数时，应答正文示例：

```text
PID,0.000700,0.000000,0.000015,220.000000,800.000000,0.000000
```

应答同样包含 `AA 55` 包头和 `55 AA` 包尾。

参数修改命令会立即保存 EEPROM，因此不要高频率连续发送相同参数，以免无谓消耗 EEPROM 写入寿命。

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
| `ERR,FRAME_TOO_LONG` | 当前帧正文超过长度限制 |
| `ERR,NOT_READY` | 循线控制器尚未初始化 |

所有错误应答也使用 `AA 55 + UTF-8 正文 + 55 AA` 格式。收到帧长度或接收溢出错误后，应重新发送完整帧。

## 7. ESP32 Arduino 示例

下面的 GPIO 编号仅为示例，应替换为实际连接的 ESP32 引脚：

```cpp
HardwareSerial stm32(2);

void sendFrame(const String &body)
{
  const uint8_t head[] = {0xAA, 0x55};
  const uint8_t tail[] = {0x55, 0xAA};

  stm32.write(head, sizeof(head));
  stm32.write(reinterpret_cast<const uint8_t *>(body.c_str()),
              body.length());
  stm32.write(tail, sizeof(tail));
}

void setup()
{
  Serial.begin(115200);
  stm32.begin(115200, SERIAL_8N1, 16, 17); // RX=16, TX=17
  sendFrame("GET");
}

void loop()
{
  static uint8_t state = 0;
  static String body;

  while (stm32.available())
  {
    uint8_t value = stm32.read();

    if ((state == 0) && (value == 0xAA))
      state = 1;
    else if ((state == 1) && (value == 0x55))
    {
      body = "";
      state = 2;
    }
    else if ((state == 2) && (value == 0x55))
      state = 3;
    else if ((state == 3) && (value == 0xAA))
    {
      Serial.println(body);
      state = 0;
    }
    else if ((state == 1) && (value == 0xAA))
      state = 1;
    else if (state == 2)
      body += static_cast<char>(value);
    else if (state == 3)
    {
      body += static_cast<char>(0x55);
      if (value == 0x55)
        state = 3;
      else
      {
        body += static_cast<char>(value);
        state = 2;
      }
    }
    else
      state = 0;
  }
}
```

## 8. 相关源码

- `Drivers/AT24CS32/`：EEPROM 页读写和 ACK 轮询；
- `Drivers/DRV8870/Src/pid_storage.c`：PID 双槽记录和 CRC32；
- `Drivers/DRV8870/Src/line_follower.c`：运行时 PID 获取、校验和更新；
- `Drivers/ESP32_PID/`：UART4 接收、命令解析和应答；
- `Core/Src/main.c`：启动加载和主循环协议处理。

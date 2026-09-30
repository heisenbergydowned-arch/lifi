# 摩斯光通信接收器：OLED、按键、完整消息、报警、RTC、HTTPS

本次代码在原 Template 工程内完成。MCU 仍是 STM32F103C8T6，光敏电阻接 PA0，ADC 仍以 1 kHz 采样，USART1 仍用于 printf。默认 T=60 ms，保留启动时实测环境光基线和亮光使 ADC 下降的极性。

## 已实现的功能

1. `Hardware/button.c/.h`：三个按键选择 T=240、120、60 ms，20 ms 消抖，一次按下只生效一次，忽略同时按多个键及上电已按住的按键。切换 T 清除未完成的码字和消息，保留已测量的光强基线。
2. `Software/MorseMessage.c/.h` 和 `MessageService.c/.h`：字母组成完整消息，每次开机从 1 编号，携带设备地址、DS1302 保存的开机计数、接收时间、T、正文和有效性标记。每条最多 96 个字符。过长或解码有误的消息明确标记，不当作有效求救消息。
3. `Hardware/buzzer.c/.h`：完整消息内出现独立单词 `SOS` 或 `HELP` 时，有源蜂鸣器报警 60 秒。大小写归一；`HELPER`、`SOS1` 不触发。新收到一条有效求救消息会从该时刻重新计时 60 秒。整个期间继续采样、解码和联网，不使用 `HAL_Delay(60000)`。
4. `Hardware/DS1302.c/.h`：GPIO 模拟三线时序，处理 BCD、12/24 小时制、日期范围、停振位、写保护。涓流充电关闭。北京时间 `+08:00`。正常启动不会覆盖已有时间。
5. `Hardware/ESP8266.c/.h`：USART3 中断收发、CRC16 帧校验、8 条消息缓存、确认和重发。ESP-01S 的独立固件位于 `ESP8266_Gateway`，负责 Wi-Fi、NTP、HTTPS/TLS 和服务器重试。
6. `Software/ReceiverDisplay.c/.h`：接入已有 `OLED` 驱动。下两行逐字显示接收内容，收完后保留整条消息；超过 32 字符每 4 秒翻页，最多 96 字符。上两行每 3 秒轮换地址/T/消息编号、RTC/ADC、ESP 最后状态/确认计数/待上报条数。报警和接收异常有专门提示，详见 `Docs/OLED_DISPLAY.md`。

## 按这个表接线

|功能|STM32 引脚|接法|
|---|---|---|
|光敏电阻分压输出|PA0|沿用原接法；亮光时 ADC 下降|
|慢速按键 T=240 ms|PA3 / Low_Mode|常开按键另一端接 GND，内部上拉|
|中速按键 T=120 ms|PA4 / Medium_Mode|同上|
|快速按键 T=60 ms|PA5 / High_Mode|同上|
|DS1302 CE / RST|PB0|接 RTC 的 CE/RST|
|DS1302 SCLK / CLK|PB1|接 RTC 的 SCLK/CLK|
|DS1302 IO / DAT|PB5|接 RTC 的 IO/DAT，额外用 4.7kΩ 上拉至 3.3V；代码使用开漏输出释放总线读取|
|OLED SCL|PB8 / OLED_SCL|软件 I²C，开漏输出；模块 SCL 上拉至 3.3V|
|OLED SDA|PB9 / OLED_SDA|软件 I²C，开漏输出；模块 SDA 上拉至 3.3V|
|OLED VCC|3.3V|沿用 128×64、I²C 7 位地址 0x3C 驱动；GND 共地|
|有源蜂鸣器模块 IN|PB6 / BUZZER|由原 PB8 移到 PB6；默认高电平响；使用带驱动的有源模块或三极管驱动，不能让 GPIO 直接带大电流负载|
|ESP-01S RX|PB10 / USART3_TX|STM32 TX 接 ESP RX|
|ESP-01S TX|PB11 / USART3_RX|STM32 RX 接 ESP TX|
|串口调试 TX|PA9 / USART1_TX|接 USB 转串口 RX，115200、8N1|
|所有模块 GND|GND|共地|

DS1302 模块按 3.3V 供电，使用配套 32.768 kHz 晶振及备份电池。固件关闭涓流充电；不要给不可充电纽扣电池充电。

ESP-01S 用稳定的 **3.3V 电源，至少 500 mA 供电能力**，不能使用 5V 逻辑或依赖供电能力不足的 USB 转串口 3.3V 小电源；供电要求见 [Espressif 官方指南](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/get-started/try-firmware/try-firmware-hardware/esp8266.html)。建议模块附近有 100 nF + 100 μF 去耦。正常运行 EN/CH_PD、RST、GPIO0、GPIO2 保持高电平；下载时按下载器说明把 GPIO0 拉低再复位，下载后恢复。常见 ESP-01S 已有部分上拉，按实际小板确认。

蜂鸣器方案是**有源模块的开关控制**；裸无源蜂鸣器只接 PB6 高电平不会持续鸣叫。若实际购买的是无源器件，需要再增加音频方波驱动。

## 完整消息如何结束

约定：点亮 T、划亮 3T；码元间灭 T，字母间灭 3T，单词间灭 7T。**连续灭灯至少 10T 才封装整条消息**，这是本实验新增的消息结束约定。

T=60 ms 时，末尾灭灯约 600 ms 后输出 `[MSG]`；T=120/240 ms 时分别约 1.2/2.4 秒。7T 的间隔保留为单词分隔，所以 `SOS HELP` 是一条含空格的消息。

手机端请选择**单次发送/关闭循环发送**，并把 T 调成与按键档位一致。旧 App 的循环间隔若只有 7T，会继续合并成一条长消息，直到停止发送后达到 10T 暗光。不能仅靠摩斯波形区分这种循环边界。

上电先让手电筒关闭约 1 秒，等环境基线标定结束，再按串口提示留出 2T 暗光后发送。切换 T 时也先停发，等待 2T 暗光再重新发送。

## 填写配置

STM32 的设备地址：`Software/MessageService.h` 中的 `DEVICE_ADDRESS`，默认 1，范围 1～65535。它代表接收点编号，不是 IP 地址。不同接收板使用不同编号。

按键三个 T 值：`Hardware/button.h` 中的 `BUTTON_SLOW_T_MS`、`BUTTON_MEDIUM_T_MS`、`BUTTON_FAST_T_MS`。上电默认 T：`Software/LdrReceiver_Config.h` 中的 `LDR_MORSE_DOT_MS`。

Wi-Fi 和网站配置已填写在本机私密文件 **`ESP8266_Gateway/include/GatewayConfig.local.h`**，由 `GatewayConfig.h` 自动包含。当前连接 `Heisenberg`，上报到 `https://lifi.hsn811.top/api/v1/messages`，绑定接收端地址 1。修改其他网络时使用下面这些宏：

```c
#define WIFI_SSID "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"
#define REPORT_HTTPS_URL "https://你的域名/api/v1/messages"
#define REPORT_DEVICE_TOKEN "你的网站给这个设备的令牌"
```

本机配置已包含 ISRG Root X1 根证书和设备专用令牌；服务器证书链已验证。分享源码时排除 `.local.h`，空白模板保留在 `GatewayConfig.h` 中。程序**不会忽略证书校验或假报上传成功**。这些设置属于 ESP 的程序；仅重新烧录 STM32 HEX 不会改变 ESP 的 Wi-Fi 参数。

ESP 使用 NTP 网络时间验证 HTTPS 证书，并经串口给 DS1302 校时；以后每小时再同步一次。主备时间服务为阿里云与腾讯云 NTP。首次收到无效网络日期会拒绝写入。RTC 未接/停振/日期无效时，上报 `time_valid=false`、`received_at=null`，另带开机毫秒数，不用编译时间冒充接收时间。历史缓存消息的原接收时间不会被后来的校时改写。

离线手动校时可在初始化 DS1302 后调用 `DS1302_SetTime()`；该接口参数是实际年月日时分秒。校时操作只做一次，避免每次复位都写回旧时间。

## 烧录与构建

- STM32：打开 `MDK-ARM/Template.uvprojx`，目标 `Template`，Keil Rebuild 后生成 `MDK-ARM/Template/Template.hex`。新增文件已加入项目。
- ESP-01S：打开 `ESP8266_Gateway` 这个 PlatformIO 工程，配置已经填好，可直接执行构建/上传。目标 `esp01s` 为 **1MB Flash**。旧 512KB ESP-01 要先核实容量，不能直接套用 1MB 烧录配置。
- ESP 使用这里提供的**自定义网关固件**，不使用出厂 AT 固件。Wi-Fi、TLS 的长操作在 ESP 上执行，STM32 不等待这些操作。
- `ESP8266_Gateway/build-check.ini` 仅验证 HTTPS 路径能编译、链接；里面用的是 `.invalid` 地址和测试证书文本。**不要烧录该测试目标**。
- `Firmware/STM32-Morse-IoT.hex` 是 STM32 镜像；**`Firmware/ESP01S-LiFi-configured.bin` 是本次可联网的 ESP 镜像，烧录起始地址 0x00000**。该镜像内含 Wi-Fi 与设备凭据，只供本人使用。`ESP01S-unconfigured.bin` 是此前保留的空配置样例，不用于本次联网。校验和在同目录。

CubeMX 已保存：三只按键、RTC GPIO、PB6 蜂鸣器、PB8/PB9 OLED 开漏输出（初始高电平）、USART3 PB10/PB11 115200、中断优先级 2。PB7 未使用；软件 I²C 不启用硬件 I²C 外设。原 ADC1/TIM3/DMA、USART1 和 SWD 配置保留。生成顺序：GPIO → DMA → USART1 → ADC1 → TIM3 → USART3；各初始化调用只有一次。OLED 初始化及首次清屏在 ADC 开始采样前完成；ADC DMA 先于 TIM3 触发启动。

## 串口观察

示例格式（编号、时间以实际接收为准）：

```text
[T] dot=120 ms; partial message discarded, wait dark >=240 ms.
[RTC] synchronized: 2026-09-10T20:00:00+08:00
[ALARM] SOS/HELP: buzzer ON for 60s from this message.
[MSG] addr=1 boot=4 id=1 time=2026-09-10T20:01:00+08:00 T=60 flags=25 text="SOS"
[NET] VPS confirmed addr=1 id=1.
```

`flags` 是位组合：1=RTC 时间有效，2=解码有错误，4=正文截断，8=求救报警，16=开机计数有效。`[RX]` 仍可看到逐字母调试信息。保留你之前关闭连续 ADC 日志的状态；要观察原始数据，把 `LDR_DEBUG_ADC` 改回 1，每 20 ms 输出一条原始 ADC，不改变 1 kHz 采样率。

只有服务器确认对应事件，才打印 `VPS confirmed`。Wi-Fi 连接或 ESP 收到串口包并不代表网站已收到。缓存为 8 条 RAM 消息，断网后会重试；队满时普通新消息会被拒绝，新的求救消息优先替换尚未发出的普通消息，并明确打印丢弃编号。若队列全是求救消息则仍可能溢出；本地报警继续工作。断电会丢失尚未上报的 RAM 缓存。

开机计数保存在 DS1302 的 RAM 0～11 字节，勿与其他用途共用；更换电池或清空 RAM 后该计数可能重新开始。没有可靠 RTC RAM 的情况下，只能保证当前供电会话内去重。

## 验证与下一步

软件验证记录见 `Docs/IOT_VERIFICATION.md`，服务器接口约定见 `Docs/IOT_PROTOCOL.md`。已有测试覆盖原解码/采样、按键/报警、RTC 引脚时序模型、消息组装、序号、校时、重发/确认及网关状态机。

网站现已部署到 **https://lifi.hsn811.top**，后台管理密码在本机 `MonitorWebsite/.private/access.txt`，源码和部署说明见 `MonitorWebsite/README.md`。已从公网验证设备上报、重复确认及冲突拒绝；一封 `LIFI TEST OK` 测试邮件已被 QQ SMTP 接受，并在收件箱中匹配到该测试邮件。板上还需要验证实际接线、电源、真实光脉冲、蜂鸣器、RTC 走时与 ESP 真实联网。当前尚未烧录硬件。

参考：驱动时序按 [DS1302 官方数据手册](https://www.analog.com/media/en/technical-documentation/data-sheets/ds1302.pdf)；HTTPS 验证按 [ESP8266 Arduino BearSSL 文档](https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/bearssl-client-secure-class.html)。

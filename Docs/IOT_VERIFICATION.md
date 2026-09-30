# IoT 扩展软件验证记录

日期：2026-09-10。对象：原 `Template`，STM32F103C8T6，以及 `ESP8266_Gateway` 的 ESP-01S 1MB 目标。

## OLED 增补验证（2026-09-10 22:24，当前 STM32 镜像）

- 按用户最终接线：PB8 OLED SCL、PB9 OLED SDA，均开漏、初始高电平；蜂鸣器从 PB8 移到 PB6，推挽、初始低电平；PB7 未使用。已由 CubeMX 配置并生成，没有手工修改生成区。
- 再次从 `.ioc` 生成后，Core/Hardware/Software/OLED 源码指纹、所有 USER CODE 块、六个 MX 初始化调用及顺序、Keil 自定义文件组/包含路径完全一致。与修改前备份比较，其余生成外设文件内容一致；生成代码仅 main.c、main.h、gpio.c 按预期变化。
- Keil 完整 Rebuild：**0 错误、0 警告**。Code=21920、RO=3260、RW=156、ZI=7644 字节；HEX 装载内容 25336/65536 字节，静态 RAM（RW+ZI）7800/20480 字节。调用图最大已知栈深仍为 288 字节，含未知库/间接调用部分，未测硬件栈峰值。
- `Tests/run_all_tests.ps1` 全部通过：保留解码器、ADC、接收器不同极性和 T 值、按键/蜂鸣器/RTC、UART/消息、ESP 网关状态机检查。IoT 集成新增快照检查：正在接收的文字、收完编号、收到服务器确认后文字保留、切 T 丢弃残文后恢复上一条、原始 ADC 与归一值区分、S1 CRC/长度边界。28551 次断言包含大量引脚事务，不代表独立场景数量。
- 新增 `test_display.c` / `run_display_tests.ps1`：运行生产显示模块，模拟屏幕和数据源；3423 次断言覆盖每次最多一次字符写入、96 字符三页、接收中跟随最新页、收完回第一页、短消息擦除旧字、报警优先级/结束恢复、无效 RTC、原始 ADC 为 0、最大 32 位编号/确认次数、切 T 回到状态页、时间计数回绕。模拟屏幕不证明真实 OLED 已点亮。
- 更新 `Firmware/STM32-Morse-IoT.hex`（71315 文件字节），校验 Intel HEX 各记录校验和、地址范围及结束记录，更新清单与 SHA256SUMS；ESP 两份 BIN 的校验和保持不变。
- 尚未烧录或测量实物。OLED 地址、显示方向、上拉、电气时序，刷新时真实 ADC 是否无缺样，以及蜂鸣器新引脚，需按 `OLED_DISPLAY.md` 上板核对。当前 OLED 驱动不读 ACK，不能自动检测屏幕是否接入。

以下为 OLED 增补前的历史记录；网站/已配置 ESP 的后续记录见 `LIFI_DEPLOYMENT.md`。

## 编译

|目标|结果|内存/镜像信息|
|---|---|---|
|Keil Template，ARMCC 5.06 update 5 build 528|完整重编译，0 错误、0 警告|Code=18984，RO=1720，RW=116，ZI=7324 字节，符合 64KB Flash / 20KB RAM|
|ESP-01S 默认未配置固件，Arduino Core 3.1.2 / PlatformIO espressif8266 4.2.1|清理后完整构建成功|静态 RAM 29260/81920；Flash 272227/761840 字节|
|ESP-01S HTTPS 编译检查目标|完整编译、链接成功，HTTPS 路径参与链接|静态 RAM 31080/81920；Flash 408211/761840 字节；该目标使用测试配置，不能烧录使用|

ESP 默认配置为空，编译器会移除部分无法执行的联网路径，因此单独构建启用这些路径的检查目标。编译检查不等于已通过真实 TLS 握手，实际根证书内容还会改变体积和运行时内存。

链接器调用图给出的最大已知栈用量为 288 字节，另有函数指针/库调用未知部分；项目仍保留原 1024 字节栈。此数字不是上板测得的栈峰值。

## 自动化测试

运行 `Tests/run_all_tests.ps1`：

- 原 MorseDecoder：279 项检查通过。
- 原 LdrAdc：11 组初始化、启停失败、DMA、环形队列、回调延迟、时钟回绕检查通过。
- 原 LdrReceiver：T=60/240 ms × 两种光强极性，校准、抗脉冲噪声、缺样恢复、硬件故障路径通过。
- 新 IoT 集成：真实生产 C 模块配合 GPIO/UART/ADC 仿真；按键抖动、长按、多键、计数回绕；60 秒报警及重新计时；DS1302 引脚级读写、首位时序、BCD、12 小时转换、闰日、停振/缺模块、开机计数；帧 CRC/长度/注入字符；队列溢出、优先保留求救、ACK 对应、超时重试、串口故障恢复。总计 28316 次断言，包含大量逐引脚事务检查，并不代表同数量的独立测试场景。
- 接收链路联动：模拟原始 ADC 530/430 → `SOS` / `HELP` → 完整编号消息 → 蜂鸣器状态 → 带 RTC 时间的串口上报。`HELPER` 不报警；有无效脉冲的消息不报警；7T 保留单词空格；T 切换和缺样不会把残文拼成 SOS。
- 网关：直接运行生产 `main.cpp`，只替换 Arduino/串口/HTTPS 环境；30 项检查覆盖断网、等待 NTP、200 缺确认、错误确认、500、201 正确确认、相同事件重试 ID 不变、重复 UART 包只补发确认、待发缓冲和超长行。HTTP、TLS 和 Wi-Fi 都是测试替身，没有访问实际网站。

另可用 `Tests/run_receiver_tests.ps1 -AdcDiagnostics` 验证打开原始 ADC 输出时的接收与电阻计算路径。

## CubeMX 重新生成

通过已安装的 CubeMX 6.18.0 命令模式设置并保存 GPIO/USART3/NVIC，再生成。最后再次从 `Template.ioc` 重新生成，比较结果：

- main.c 所有 USER CODE 块内容一致（比较时统一 CRLF/LF）。
- Hardware/Software 自定义文件 SHA-256 一致。
- Keil 目标、包含目录、文件分组和源文件清单一致。
- `MX_GPIO_Init`、`MX_DMA_Init`、`MX_USART1_UART_Init`、`MX_ADC1_Init`、`MX_TIM3_Init`、`MX_USART3_UART_Init` 各调用一次，次序保留。
- 生成的 USART3 中断入口调用 HAL UART 中断处理；自定义回调只处理 huart3。ADC 回调仍由 LdrAdc 独占。

重新生成后再次完成 Keil 全量重编译。没有手工修改 `ProjectManager.functionlistsort` 或把外设初始化搬出生成区。

资源所有权表见 `IOT_OWNERSHIP.md`。构建日志、测试日志及重新生成比较结果保存在本目录的 `verification/`。固件 SHA-256 与最终源码摘要见 `../Firmware/`。

## 尚未测量

初版记录：当时未烧录 STM32/ESP，也未部署网站。2026-09-10 后续完成了 `lifi.hsn811.top` 部署、公网 HTTPS 接口验证与 QQ 测试邮件；最新结果见 `LIFI_DEPLOYMENT.md`。真实光敏电阻脉冲响应、按键电平、蜂鸣器声响、DS1302 走时精度及 ESP 板上联调仍未验证。软件模拟和电脑访问 VPS 不能代替硬件验收。

修改前完整备份位于：`C:\Users\Heisen\Documents\Codex\2026-09-08\idea\work\stm32-iot-before-20260910-201747\Template`。

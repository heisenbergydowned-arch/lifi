# 资源所有权（修改前确定）

|资源|CubeMX 初始化|运行期模块|中断/回调|顺序|
|---|---|---|---|---|
|PA0 ADC1 + TIM3 1 kHz + DMA1 CH1|MX_ADC1_Init / MX_TIM3_Init / MX_DMA_Init|LdrAdc|ADC 半满、全满、错误回调|DMA 先启动，TIM3 后启动|
|PA3 / PA4 / PA5 上拉按键|MX_GPIO_Init|button|无，主循环消抖|GPIO 后初始化，选择 240 / 120 / 60 ms|
|PB6 有源蜂鸣器控制（默认高有效，由 PB8 移入）|MX_GPIO_Init|buzzer|无，主循环计时|GPIO 后关闭；消息层触发 60 s|
|PB8 OLED_SCL / PB9 OLED_SDA，开漏、初始高电平|MX_GPIO_Init|OLED 驱动 + ReceiverDisplay 界面|无；软件 I²C，主循环每次最多绘制一个变化的字符|初始延时和清屏在 ADC 启动之前完成；运行期不整屏清屏、不关中断|
|PB0 CE / PB1 SCLK / PB5 IO DS1302|MX_GPIO_Init|DS1302|无，GPIO 软件时序|GPIO 后；IO 开漏释放读取，外接 4.7k 上拉到 3.3V|
|USART1 PA9/PA10 调试|MX_USART1_UART_Init|Hardware/USART1|保留已有 DMA|先于 printf|
|USART3 PB10 TX / PB11 RX → ESP-01S|MX_USART3_UART_Init|ESP8266|USART3 IRQ / UART RX、TX、错误回调，均筛选 huart3|串口初始化后启动接收，非阻塞发送|
|字母、完整消息、求救词、编号|无|MorseMessage + MessageService|无|接收器事件交给消息层；报警不等待网络|
|屏幕文字及状态快照|无|ReceiverDisplay|无|MessageService 保持唯一接收事件订阅者；显示只读快照，不接管 UART/ADC 回调；RTC 每秒最多读一次|
|HTTPS / Wi-Fi / TLS / 重试|ESP8266 独立固件|MorseGateway|ESP 网络栈|STM32 仅串口交接，VPS 留配置|

边界：原始摩斯只有字母/单词间隔，无天然消息帧。本版以连续暗光 >=10T 作为本实验的消息结束约定；7T 仍是词间隔。发送多条完整消息应单次发送并留至少 10T 暗光。切换 T、ADC 缺样或解码错误不会把两段残文拼成有效求救词。RAM 队列有限，溢出必须明确记录。

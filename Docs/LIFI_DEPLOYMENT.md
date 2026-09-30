# LiFi 网站与 ESP 网关交付记录

日期：2026-09-10；网站：https://lifi.hsn811.top；接收邮箱：1743858164@qq.com。

## 软件与公网验证

|项目|证据|
|---|---|
|ESP-01S 1MB，160MHz，自定义网关固件|PlatformIO espressif8266 4.2.1、Arduino Core 3.1.2 构建成功|
|资源占用|静态 RAM 32968/81920；Flash 410139/761840 字节|
|主机网关测试|实际 main.cpp 编译到模拟 HAL/HTTPS 环境，30 检查通过|
|服务器测试|21 项 unittest：并发去重、持久确认、冲突拒绝、求救边界、认证、邮件不确定状态等|
|公网 API|新消息 201，相同重试 200，内容变化 409；仅成功时返回匹配 X-Morse-Ack|
|未登录保护|`/api/dashboard`、`/api/messages` 返回 401|
|页面交互|在本地隔离数据中实际验证登录、文字搜索、求救筛选、详情、告警确认和退出；窄屏消息卡片无横向溢出；测试环境关闭邮件发送线程|
|HTTPS|域名匹配成功；证书链 YR2 → Root YR → ISRG Root X1，验证通过|
|网站运行|lifi 普通用户；监听回环地址；服务自启动；邮件配置与数据库 600 权限|
|已有服务|Nginx、齐鲁牌友、x-ui 保持 active；pai/school/fitting 网站返回 200|
|测试邮件|2026-09-10 21:45:48 北京时间，QQ SMTP 接受，attempts=1；随后只读搜索目标收件箱，匹配到 1 封指定测试邮件|

邮件主题：**【LiFi 系统测试】测试数据通知**；内容 `LIFI TEST OK`；事件编号 `test-initial-test-20260910-one-email`。上线验证另外保存了 `API CHECK` 接口测试记录，不发送邮件。两条都明确标为测试，不算实物上报。

详细结果：`verification/lifi-live-status.json`、`verification/lifi-live-ingest.json` 与 `verification/lifi-email-delivery.json`。固件 SHA-256 见 `../Firmware/SHA256SUMS.txt`。网站最终公开文件逐项与本机源码摘要相符。上线接口的临时设备凭据已撤销，实际地址 1 的令牌继续供 ESP 使用。

## 交付与实物步骤

1. ESP 烧录 `Firmware/ESP01S-LiFi-configured.bin`，起始地址 `0x00000`。已包含指定 Wi-Fi、HTTPS 地址、接收端 1 的专用令牌和根 CA；不需要 AT 指令固件。
2. 使用独立可靠的 3.3V 电源，ESP TX→STM32 PB11，ESP RX←STM32 PB10，GND 共地；串口 115200 8N1。下载后 GPIO0 恢复高电平再复位。
3. STM32 使用 `Firmware/STM32-Morse-IoT.hex`。当前默认 T=60 ms；手机 T 同步，关闭循环发送。
4. 等待环境基线采集和网络校时；发 HELLO，观察 `[MSG]`、`VPS confirmed` 与网站地址 1 新记录。
5. 发完整 SOS / HELP 验证蜂鸣器、网站和邮件。测试期间实际光脉宽、光敏电阻响应及阈值仍以板上数据为准。

本次未对任何实物烧录，也没有验证真实 ESP TLS/无线连接。数据缓存为 STM32/ESP RAM，未上报消息断电会丢失；服务器已确认的消息保存在 SQLite 中。

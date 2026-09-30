# 接收设备 → ESP-01S → VPS 接口约定 v1

## 串口

USART3 115200、8N1。STM32 PB10 TX → ESP RX，PB11 RX ← ESP TX。ASCII 行，以 `\n` 结束；最大整行 255 字节加字符串终止符。格式是 `正文*CCCC\n`，CCCC 为正文的 CRC16/CCITT-FALSE，初始值 FFFF、多项式 1021、不反射、无末尾异或，以 4 位大写十六进制表示。校验范围不含星号和换行。

正文类型：

|方向|正文|意义|
|---|---|---|
|STM32 → ESP|`H1\|address\|boot`|开机握手，触发时间回送|
|STM32 → ESP|`M1\|address\|boot\|sequence\|uptime_ms\|t_ms\|flags\|timestamp\|text`|完整消息|
|ESP → STM32|`A1\|address\|boot\|sequence\|wire_crc`|VPS 确认；wire_crc 是对应 M1 正文 CRC 的十进制值|
|ESP → STM32|`T1\|YYYY-MM-DDTHH:mm:ss+08:00`|NTP 提供的 RTC 校时值|
|ESP → STM32|`S1\|状态文字`|状态，不当成服务器确认|

没有有效时间时 timestamp 为单个 `-`。正文仅允许 `A-Z`、`0-9`、空格和 `+ = . , ? / -`，不允许竖线、星号、引号、控制字符。编号为 1～UINT32_MAX；设备地址 1～65535；T=60～2000 ms。解析拒绝字段溢出、坏 CRC、非法日期、缺字段、多字段和无效标记组合。

ESP8266 模块只保留一个当前上传请求；STM32 保留至多 8 条消息，收到准确匹配地址、开机计数、编号、CRC 的 ACK 后才移除队首。60 秒未收到 ACK 会重发相同消息。ESP 自己以最长 60 秒的退避间隔重试 HTTPS，同一消息使用不变的 `event_id`。UART 重复包不会绕过 ESP 的退避。

## HTTPS

设备主动请求配置的地址，例如 `POST /api/v1/messages`。

请求头：

```text
Content-Type: application/json
Authorization: Bearer <每个设备自己的令牌>
Idempotency-Key: <event_id>
```

JSON 示例：

```json
{
  "schema": 1,
  "event_id": "a1-b4-m1-t2026-09-10T20:01:00+08:00-u64000-c1234",
  "address": 1,
  "boot": 4,
  "message_id": 1,
  "uptime_ms": 64000,
  "t_ms": 60,
  "time_valid": true,
  "received_at": "2026-09-10T20:01:00+08:00",
  "boot_counter_valid": true,
  "decode_valid": true,
  "truncated": false,
  "alarm": true,
  "text": "SOS"
}
```

这是格式示例，CRC/事件编号应使用固件实际生成值。无有效 RTC 时间时 `received_at` 为 JSON null。网站另存服务器入库时间 `ingested_at`，不要覆盖设备的 `received_at`，也不要把开机毫秒数当作日历时间。

服务器处理顺序：

1. 校验令牌与设备地址的对应关系、请求大小、schema 和字段类型。
2. 校验 `Idempotency-Key` 与 JSON `event_id` 相等。按 event_id 唯一约束入库，保存正文/时间/有效性等内容；已有相同事件且正文相同则返回原确认，冲突则报错。
3. 求救判断采用有效、未截断消息中的独立 SOS/HELP 单词。报警通知任务与首次入库关联；重试不得再次发邮件。建议在同一事务中保存通知待办，事务提交后再由工作进程执行通知。
4. **数据库提交成功后**，首次入库返回 HTTP 201，已存在的相同消息返回 HTTP 200，响应头必须包含：

```text
X-Morse-Ack: <与请求完全一致的 event_id>
```

响应正文可为 `{"ok":true}`。网关同时要求 200/201 和严格匹配的 `X-Morse-Ack`，否则仍保留请求。502、超时、401、登录页面的 200、缺少 ACK 或 ACK 不匹配均不是成功。代理需要原样保留该响应头。

网关验证 HTTPS 主机名和证书链，联网获取有效系统时间后才发起 TLS 请求；不跟随重定向。根 CA 随 VPS 域名的实际证书链配置。网页、数据库、提醒通道将在服务器阶段实现，本次没有调用 QQ、邮箱或手机消息接口。

## 标识与断电边界

`message_id` 是每次 MCU 启动内的递增显示编号，不能单独当数据库主键。带有效 DS1302 开机计数时，event_id 结合地址、开机计数、消息序号及原始时间字段，ESP 重启后对相同缓存消息仍可生成相同 ID。

RTC RAM 不可靠时，网关改用当前启动随机会话号。此时跨设备复位的严格去重不保证；断电待发消息也没有持久化。本课程版本通过 RAM 缓存和确认重发处理普通断网，不声称断电零丢失或真实救援级保障。

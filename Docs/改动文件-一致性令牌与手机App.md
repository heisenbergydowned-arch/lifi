# 改动文件清单：一致性令牌、ProtocolConfig 四元组与手机 App

> 本文档记录 C1（服务器/网关一致性令牌）+ C2（dash_t 独立可调 + ProtocolConfig 四元组）在
> **三端（服务器 / 网关 / STM32）** 的全部改动与部署约束。
> STM32 端已实现并全量验证（2026-09-30）；网关端与服务器/网页/App 端按本文档第 4 节规划实施。

---

## 1. 背景与目标

- 现状：摩斯时序只有 `t_ms`（点时长 T）一个可调参数，dash 固定 3T、字间距固定 4T、消息结束固定 10T。
  发端（Morsify App 或人工按键）与收端（STM32 解码器）若时序不匹配，长划会被误判为超长脉冲丢弃。
- 目标：
  1. 把 `{t_ms, dash_t, word_gap_t, msg_gap_t}` 做成**全局 ProtocolConfig 四元组**，三端单一事实源。
  2. 每次写入生成**一致性令牌**（token），三端可各自计算并比对，及时发现配置漂移。
  3. 手机 App（lifiapp.jjqpro.com，Cloudflare **灰云** = 仅 DNS 直连 VPS）成为第三个配置写入方，
     与网页管理站共用同一写入 API 与校验规则。
- 不新增引脚、不改 CubeMX 配置（`.ioc` 是外设唯一来源）。

## 2. 勘察现状（VPS / 域名 / 三端）

| 项 | 现状 |
|---|---|
| VPS | Ubuntu 24.04.4，107.175.140.28；nginx SNI：443 → 8444 |
| 生产部署 | `/opt/lifi/current → releases/20260910-1.0.0-final`（v1.0.0，**无 C1 端点**） |
| 数据库 | SQLite，**无** device_settings 表（需随本次部署创建） |
| 本地 server.py | 已升至 v1.1.0：含 `/app/api/protocol` GET/PUT、device_settings 建表、token 生成（本地领先生产，未部署） |
| lifi.hsn811.top | 绿云（CDN），生产监控站 |
| lifiapp.jjqpro.com | 灰云（DNS-only，直连 VPS 8444），App 域名，已确认为灰云 |
| STM32 固件 | 本轮起支持 C1 下行帧（四元组），宿主测试全绿 |
| ESP-01S 网关 | 现状仅转发上行；**待改**：下行 C1 帧 + token 计算 |

## 3. 总体设计

### 3.1 ProtocolConfig 四元组

| 参数 | 含义 | 范围 | 默认 | 推导时序（固件计算） |
|---|---|---|---|---|
| `t_ms` | 点时长 T (ms) | 60–2000 | 60 | 基础时钟单位 |
| `dash_t` | 划时长倍数 | 2–5 | 3 | dash = dash_t × T |
| `word_gap_t` | 字间距倍数 | 2–8 | 4 | 字间隙 ≥ word_gap_t × T |
| `msg_gap_t` | 消息结束暗段倍数 | 4–20 | 10 | 暗段 ≥ msg_gap_t × T 判消息结束 |

固定不变：噪声底 T/4；字符刷新 2T 暗段；点划判界 = (1 + dash_t) / 2 × T；超长脉冲上限 = 2 × dash_t × T。

**向后兼容**：`dash_t=3 / word_gap_t=4 / msg_gap_t=10` 精确复现原 2T 判界 / 6T 上限 / 4T 字间 / 10T 消息结束。

### 3.2 一致性令牌（token）

```
token = sha256("t_ms:dash_t:word_gap_t:msg_gap_t") 取前 16 位 hex
```

- 例：`sha256("60:3:4:10")` 的前 16 位。
- 计算责任：服务器（写入时生成并返回）、网关（收到 C1 帧后本地重算比对）、App/网页（展示）。
- **STM32 端不计算、不回传 token**——固件资源有限且四元组本身已校验，令牌比对是服务端/网关职责。
- 任何一端的 token 与服务器返回值不一致 → 配置漂移告警（网页显示 / 网关状态行）。

### 3.3 C1 下行帧（网关 → STM32，USART3）

```
C1|<t_ms>|<dash_t>|<word_gap_t>|<msg_gap_t>\r\n
```

- 5 字段；任一解析失败或越界 → **整帧丢弃、保留旧配置**（静默，绝不部分应用）。
- 校验链：`IotProtocol_U32`（每字段）→ `MORSE_DECODER_*_MIN/MAX` 范围 → 回调 `ESP8266_ConfigHandler`。
- 旧固件收到 C1 帧：HandleLine 无匹配分支，静默忽略——**下行新帧对旧固件安全**。

### 3.4 数据流

```
手机 App (灰云 lifiapp.jjqpro.com)          网页管理站 (lifi.hsn811.top)
        │  PUT /app/api/protocol                  │  PUT /api/settings
        └──────────────────┬───────────────────────┘
                           ▼
              FastAPI 服务器（校验四元组 → 存 device_settings → 返回 token）
                           │  下行 C1 帧 + token
                           ▼
                  ESP-01S 网关（重算 token 比对 → 转发 C1 帧到 USART3）
                           ▼
        STM32：HandleLine C1 分支 → 范围校验 → ApplyProtocolConfig
              → LdrReceiver_SetProtocolParams（幂等）→ 解码器重初始化
              → OLED "A1 T60 D3" + 串口 [T] 打印
```

### 3.5 App API

| 方法/路径 | 说明 |
|---|---|
| `PUT /app/api/protocol` | 写入四元组（body: 4 个整数）；校验范围，落库，返回 `{token}` |
| `GET /app/api/protocol` | 读取当前四元组 + token（App 启动时拉取，决定本地发送时序） |

- App 写入前必须按 3.2 规则本地重算 token 与返回值比对，失败提示"配置未同步"。
- App 发送端时序（Morsify Flash 模式）必须用同一四元组生成光码。

## 4. 分文件改动清单

### 4.1 STM32 端（已完成，宿主测试全绿）

| 文件 | 改动 |
|---|---|
| `Software/MorseDecoder.h` | 新增 DASH_T / WORD_GAP_T / MSG_GAP_T 的 MIN/MAX/DEFAULT 常数；struct 加 `dash_t, word_gap_t, msg_gap_t`；`MorseDecoder_Init` 改 4 参 |
| `Software/MorseDecoder.c` | `ClampParam`（0→默认，越界→钳 min/max）；判界改双倍比较 `2*duration < (1+dash_t)*T`（半 T 精度）；两处超长上限改 `2*dash_t*T`；word gap / msg idle 用参数；`Reset` 保持四值 |
| `Software/LdrReceiver.h` | `LdrReceiver_SetProtocolParams(4×uint32_t)` 声明；Status 结构加 `dash_t, word_gap_t, msg_gap_t` |
| `Software/LdrReceiver.c` | `SetProtocolParams`：任一非法→false 保旧配置；全等→幂等 true；生效→Init+Reset+丢弃半截消息+`[T] t=.. dash=.. wg=.. mg=..` 打印；`SetDotTimeMs` 降为薄包装（只改 T 保 gaps） |
| `Hardware/ESP8266.h` | `ESP8266_ConfigHandler` typedef（4×uint32_t）+ `ESP8266_SetConfigHandler` |
| `Hardware/ESP8266.c` | HandleLine 新增 C1 分支：count==5、`IotProtocol_U32`×4、范围校验、失败静默 return、成功调 handler；include MorseDecoder.h |
| `Core/Src/main.c` | BEGIN 0 新增 `ApplyProtocolConfig`（转调 SetProtocolParams）；BEGIN 2 注册 `ESP8266_SetConfigHandler(ApplyProtocolConfig)` |
| `Software/ReceiverDisplay.c` | 状态页 0 行改 `"A%u T%lu D%lu"`（地址/T/dash_t，13 字符≤16 列）；`shown_dash_t` 变更追踪；Init 清零 |
| `Tests/test_morse_decoder.c` | 3 处 Init 调用点改 4 参；新增：判界 1.5T/2.5T/3T（dash_t=2/4/5）、超长上限 4T/8T/10T、自定义 gaps（wg=5/mg=12）、Init 钳制与 Reset 保持 |
| `Tests/test_display.c` | OLED 行期望改 `"A1 T60 D3"` / `"A1 T120 D3"`；Reset 助手补 `dash_t=3` |

测试入口：`Tests\run_all_tests.ps1`（MinGW gcc `-std=c99 -Wall -Wextra -Werror -pedantic -O2`，%TEMP% 临时目录）。

### 4.2 网关端（待实施）

| 文件 | 改动 |
|---|---|
| `ESP8266_Gateway/` 配置存储 | device_settings 持久化（flash），掉电保留，缺省 = 默认四元组 |
| 配置接收 | 服务器下行四元组 → 校验范围（同 3.1 表）→ 存 flash → 经 USART3 发 `C1|...` 帧 |
| token | 本地实现 sha256（或 16 位截取），重算比对服务器返回值，不一致写告警状态 |
| 状态上报 | 上行状态行追加 token 前 8 位（供服务器核对） |

### 4.3 服务器 / 网页 / App（待实施）

| 项 | 改动 |
|---|---|
| `MonitorWebsite/server.py` | 本次完成：device_settings 建表、`/app/api/protocol` GET/PUT、token 生成、VERSION 1.1.0；测试 26 个全绿 |
| 网页管理站 | 设置页四元组表单（带范围提示）+ 显示 token 与三端比对结果 |
| 手机 App | 启动 GET 四元组；发送时序用同一参数；设置页写入后本地 token 比对 |
| nginx | lifiapp.jjqpro.com server block：SNI → 8444（灰云直连，无 CDN） |

## 5. E2E 验收（5 步，t=80 / dash=4 / wg=5 / mg=12）

1. 网页 PUT 四元组 → 服务器落库，返回 token（记为 A）。
2. App GET → 得四元组 + token，本地重算 = A。
3. 网关收到下行，重算 token = A，经 USART3 发 `C1|80|4|5|12`。
4. STM32 打印 `[T] t=80 dash=4 wg=5 mg=12`，OLED 显示 `A1 T80 D4`。
5. App 按同一四元组发 "TEST"，STM32 完整解码上报，无超长丢弃。

## 6. 兼容与迁移

- **C1 与 C2 必须一起部署到 VPS**：生产 v1.0.0 无 C1 端点，若只上服务器侧新端点而网关/固件仍旧，
  链路半新半旧，token 无法闭环。
- 旧固件收 C1 帧静默忽略；新固件在旧服务器下收不到 C1 帧，跑默认四元组——**两个方向都安全**。
- 掉电行为：STM32 配置 RAM-only，掉电回默认（设计如此）；网关 flash 持久化，上电重发 C1 帧恢复。

## 7. 测试计划

- STM32 宿主（已完成全绿）：decoder 341 checks；LdrAdc 12 组；LDR receiver 4 组×13 用例；
  按钮/蜂鸣器；DS1302；IotProtocol 28551 断言；网关状态机 30 checks；display 3419 断言。
- 网关：mock HTTPS 下 C1 下发 + token 比对 + flash 持久化用例。
- 服务器：范围边界（0/59/60/2000/2001 等）+ token 固定向量。
- E2E：按第 5 节 5 步，真机 + 真实 VPS。

## 8. 回滚

- VPS：`/opt/lifi/current` 软链回指上一个 release 目录，nginx reload 即可（无 schema 破坏前，
  device_settings 表保留不碍事）。
- STM32/网关固件：刷回上一版本固件即可；C1 帧对旧固件安全（静默忽略）。
- 数据库：device_settings 为新增表，回滚时无需删除。

## 9. 风险与决策

| 风险 | 决策 |
|---|---|
| 灰云 = 无 CDN，App 域名直连 VPS IP 暴露 | 已确认接受（灰云为当前状态）；后续可切绿云或加 WAF |
| 部分应用（四元组改了一半） | 帧级严格校验 + 整帧丢弃，从机制上排除 |
| 超长脉冲上限放宽（dash_t=5 → 10T）误收噪声 | 上限严格 `>` 比较；噪声底 T/4 与 2T 字符刷新不变 |
| 判界半 T 精度（1.5T/2.5T） | 双倍整数比较，无浮点、无溢出（2×2000×5 = 20000 < 2^32） |
| token 算法三端实现不一致 | 固定字符串格式 `"t:d:wg:mg"` + sha256 前 16 位，测试用固定向量钉死 |

## 10. 不覆盖项

- 多设备独立四元组（现按单设备配置设计，device_settings 表结构预留扩展）。
- STM32 端 token 计算/回传（明确不做，见 3.2）。
- App 发送端（Morsify 光码生成器）的具体实现——本文档只约束其必须使用同一四元组。
- VPS 侧 TLS 证书轮换、监控告警链路——沿用现有方案。

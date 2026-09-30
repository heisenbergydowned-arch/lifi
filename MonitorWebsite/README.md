# 光信站 · LiFi Monitor 1.0.0

已部署：**https://lifi.hsn811.top**。登录密码在本机 `.private/access.txt`。不需要填写 VPS 的 root 密码来登录网站。

## 已实现

- 管理登录、12 小时会话、HTTPS、请求校验和设备令牌绑定。
- 消息列表、文本搜索、求救/普通/测试筛选、分页与详情。
- 地址编号、消息编号、T、RTC 接收时间和服务器入库时间分别显示。
- 独立单词 SOS / HELP 触发网页告警和邮件；译码异常或截断的消息不会报警。
- 确认告警后保留历史；可主动开启当前页面的声音提醒。
- QQ / Resend SMTP 设置，默认 QQ 邮箱向 `1743858164@qq.com` 发送通知。
- SQLite 持久保存；一次有效上报对应一个事件和最多一个初始邮件任务。
- 只有数据库提交成功才返回 `X-Morse-Ack`；相同内容重试返回 200，编号相同但内容不同返回 409。
- 邮件在后台发送。连接失败最多尝试 3 次；提交途中断线或进程重启时标记“结果待确认”，避免自动重复发信。

后台每 5 秒更新页面。设备当前仅在有完整消息时上报，因此“最近上报”不能当作持续在线状态。网页声音需用户点击开启，后台手机推送和 QQ 聊天消息尚未实现；当前通知渠道是电子邮件。

## 文件与服务

|用途|位置|
|---|---|
|后端、消息校验、数据库、邮件任务|`server.py`|
|中文监测页面|`static/`|
|本机管理登录信息|`.private/access.txt`，不要上传到网站目录|
|服务器私密配置|`/etc/lifi/config.json`，640 root:lifi|
|邮件授权码|`/var/lib/lifi/mail.json`，600 lifi:lifi，仅服务器后端读取|
|持久数据库|`/var/lib/lifi/lifi.sqlite3`，600 lifi:lifi|
|部署目录|`/opt/lifi/current` → `releases/20260910-1.0.0-final`|
|服务|`lifi.service`，普通 lifi 用户、单 worker、监听 127.0.0.1:8791|
|站点配置|`/etc/nginx/conf.d/lifi.conf`|
|证书|`/etc/letsencrypt/live/lifi.hsn811.top/`|

本 VPS 已有 SNI 分流：公网 443 经 Nginx stream 转发到本地 8444，新增站点沿用这一结构。`deploy/lifi.nginx.conf` 是针对这台 VPS 的配置，换服务器时需根据实际监听端口调整。

证书通过已有 Certbot timer 自动续期，`/etc/letsencrypt/renewal-hooks/deploy/lifi-nginx.sh` 在成功更新后检查并重载 Nginx。新增站点前的配置备份位于 `/opt/lifi-backups/20260910-initial/`。

## 本地运行与测试

Python 3.12+。生产依赖精确版本在 `requirements.lock.txt`，测试依赖另装 `httpx`。

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.lock.txt httpx
.venv\Scripts\python -m unittest discover -s tests -v
```

本地运行需要私有配置，设置 `LIFI_CONFIG` 为配置 JSON 路径，`data_dir` 改为本机目录，`origin` 改为本地地址；仅本地 HTTP 调试可设置 `secure_cookie=false`。不要向本地数据目录复制生产邮件任务，也不要在测试中使用真实 SMTP 授权码。

```powershell
.venv\Scripts\python -m uvicorn server:create_app --factory --host 127.0.0.1 --port 8791
```

生产使用 `deploy/lifi.service`，禁止多 worker 共享一个邮件队列的旧发送进程；单 worker 内 HTTP 与邮件后台线程独立。

## 上报协议

详见 `../Docs/IOT_PROTOCOL.md`。默认硬件地址 1。65535 为本次上线的接口联调地址，其测试记录明确标为“系统测试”；测试令牌在验收结束后撤销。

设备接收时间由 DS1302 提供；未校时就传 null。网站自己的服务器时间始终单独记录，不补造 RTC 时间。网页的信号旅程是流程说明，当前并不上传 ADC 波形。

## 本次验收

21 项服务器自动化测试通过；ESP 实际源文件的 30 项网关状态机测试通过；配置好的 ESP-01S 固件编译成功。公网实测新消息 201、重试 200、内容冲突 409，确认头一致。测试邮件仅创建一个任务、发送一次，并在目标 QQ 收件箱匹配到该测试消息。

尚未烧录、连接 ESP / STM32 实物，软件与公网验证不代表板上联调已完成。先刷 ESP 配置固件，连接接收端，再用手机关闭循环发送、T=60 ms 发 `HELLO`；网页应出现硬件地址 1 的新记录。发 `SOS` 后验证本地蜂鸣器 60 秒、网页告警与邮件。

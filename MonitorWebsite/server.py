"""LiFi monitor: authenticated ingestion, durable acknowledgements and mail outbox."""
import asyncio
import hashlib
import hmac
import json
import os
import re
import secrets
import smtplib
import sqlite3
import ssl
import threading
import time
from collections import defaultdict, deque
from contextlib import asynccontextmanager, contextmanager
from datetime import datetime, timezone
from email.message import EmailMessage
from email.utils import formatdate
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

HERE = Path(__file__).resolve().parent
VERSION = '1.1.0'
FIELDS = {'schema', 'event_id', 'address', 'boot', 'message_id', 'uptime_ms', 't_ms',
          'time_valid', 'received_at', 'boot_counter_valid', 'decode_valid', 'truncated', 'alarm', 'text'}
# ProtocolConfig 四元组：与 MorseDecoder.h / ProtocolConfig.c 单一事实源保持一致。
PROTOCOL_FIELDS = ('t_ms', 'dash_t', 'word_gap_t', 'msg_gap_t')
PROTOCOL_LIMITS = {'t_ms': (60, 2000), 'dash_t': (2, 5), 'word_gap_t': (2, 8), 'msg_gap_t': (4, 20)}
PROTOCOL_DEFAULTS = {'t_ms': 60, 'dash_t': 3, 'word_gap_t': 4, 'msg_gap_t': 10}


def utcnow():
    return datetime.now(timezone.utc).isoformat(timespec='seconds')


def digest(value):
    return hashlib.sha256(value.encode()).hexdigest()


def protocol_token(t_ms, dash_t, word_gap_t, msg_gap_t):
    # 与 ProtocolConfig.c 的 ComputeToken 完全一致：sha256("t_ms:dash_t:word_gap_t:msg_gap_t") 前 16 位。
    return digest(f"{t_ms}:{dash_t}:{word_gap_t}:{msg_gap_t}")[:16]


def password_hash(password, salt=None):
    salt = salt or secrets.token_hex(16)
    result = hashlib.pbkdf2_hmac('sha256', password.encode(), bytes.fromhex(salt), 600000).hex()
    return salt + ':' + result


def check_password(password, encoded):
    return hmac.compare_digest(password_hash(password, encoded.split(':')[0]), encoded)


def validate_message(data, address, key):
    if type(data) is not dict or set(data) != FIELDS:
        raise HTTPException(422, '消息字段不完整或包含未知字段')
    limits = {'schema': (1, 1), 'address': (1, 65535), 'boot': (0, 4294967295),
              'message_id': (1, 4294967295), 'uptime_ms': (0, 4294967295), 't_ms': (20, 5000)}
    for name, (low, high) in limits.items():
        if type(data[name]) is not int or not low <= data[name] <= high:
            raise HTTPException(422, '无效数值字段: ' + name)
    for name in ('time_valid', 'boot_counter_valid', 'decode_valid', 'truncated', 'alarm'):
        if type(data[name]) is not bool:
            raise HTTPException(422, '无效布尔字段: ' + name)
    if data['address'] != address:
        raise HTTPException(403, '设备编号与凭据不匹配')
    eid = data['event_id']
    if not isinstance(eid, str) or not re.fullmatch(r'[A-Za-z0-9:.+_-]{1,127}', eid) or eid != key:
        raise HTTPException(422, '事件编号与 Idempotency-Key 不匹配')
    if not isinstance(data['text'], str) or not re.fullmatch(r'[A-Z0-9 +=.,?/\-]{1,96}', data['text']) or not data['text'].strip():
        raise HTTPException(422, '无效摩斯文本')
    if data['time_valid']:
        stamp = data['received_at']
        if not isinstance(stamp, str) or not re.fullmatch(r'20\d\d-\d\d-\d\dT\d\d:\d\d:\d\d\+08:00', stamp):
            raise HTTPException(422, '无效 RTC 时间格式')
        try:
            datetime.fromisoformat(stamp)
        except ValueError:
            raise HTTPException(422, '无效 RTC 日期')
    elif data['received_at'] is not None:
        raise HTTPException(422, '未校时的 RTC 时间必须为 null')
    # Re-evaluate independently; punctuation delimits words just like MorseMessage.c.
    alarm = bool(data['decode_valid'] and not data['truncated'] and
                 re.search(r'(?<![A-Z0-9])(SOS|HELP)(?![A-Z0-9])', data['text']))
    return alarm


class Store:
    def __init__(self, config):
        self.config = config
        self.path = Path(config['data_dir'])
        self.path.mkdir(parents=True, exist_ok=True)
        self.mail_lock = threading.Lock()
        self.db_path = self.path / 'lifi.sqlite3'
        self.login_attempts = defaultdict(deque)
        with self.db() as db:
            db.execute('PRAGMA journal_mode=WAL')
            db.executescript('''
                CREATE TABLE IF NOT EXISTS messages (
                    id INTEGER PRIMARY KEY AUTOINCREMENT, event_id TEXT NOT NULL UNIQUE,
                    content_hash TEXT NOT NULL, payload TEXT NOT NULL, address INTEGER NOT NULL,
                    text TEXT NOT NULL, alarm INTEGER NOT NULL, is_test INTEGER NOT NULL DEFAULT 0,
                    received_at TEXT, ingested_at TEXT NOT NULL, acknowledged_at TEXT);
                CREATE INDEX IF NOT EXISTS messages_alarm ON messages(alarm,id);
                CREATE TABLE IF NOT EXISTS outbox (
                    id INTEGER PRIMARY KEY AUTOINCREMENT, event_id TEXT NOT NULL UNIQUE REFERENCES messages(event_id),
                    recipient TEXT NOT NULL, status TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0,
                    next_attempt REAL NOT NULL DEFAULT 0, last_error TEXT, sent_at TEXT,
                    created_at TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS sessions (token_hash TEXT PRIMARY KEY, csrf TEXT NOT NULL, expires REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS device_settings (
                    address INTEGER PRIMARY KEY, t_ms INTEGER NOT NULL, dash_t INTEGER NOT NULL,
                    word_gap_t INTEGER NOT NULL, msg_gap_t INTEGER NOT NULL, updated_at TEXT NOT NULL);
            ''')
            # A process can die after SMTP accepts DATA. Do not silently send twice.
            db.execute("UPDATE outbox SET status='uncertain',last_error='发送中服务重启，结果待人工确认，未自动重发' WHERE status='sending'")

    @contextmanager
    def db(self):
        db = sqlite3.connect(self.db_path, timeout=5)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA foreign_keys=ON')
        db.execute('PRAGMA synchronous=FULL')
        try:
            with db:
                yield db
        finally:
            db.close()

    def mail_settings(self):
        target = self.path / 'mail.json'
        if target.exists():
            return json.loads(target.read_text(encoding='utf-8'))
        return {'provider': 'qq', 'sender': self.config['recipient'], 'password': '', 'enabled': True}

    def mail_public(self):
        settings = self.mail_settings()
        return {k: v for k, v in settings.items() if k != 'password'} | {
            'configured': bool(settings.get('password')),
            'recipient': self.config['recipient']}

    def save_mail(self, data):
        if type(data) is not dict or set(data) != {'provider', 'sender', 'password', 'enabled'}:
            raise HTTPException(422, '无效邮件配置')
        if data['provider'] not in ('qq', 'resend') or type(data['enabled']) is not bool:
            raise HTTPException(422, '无效发信服务')
        if not isinstance(data['sender'], str) or not re.fullmatch(r'[A-Za-z0-9._+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}', data['sender']):
            raise HTTPException(422, '请输入有效的发信邮箱')
        if data['provider'] == 'qq' and not data['sender'].lower().endswith('@qq.com'):
            raise HTTPException(422, 'QQ 服务请填写 QQ 发信邮箱')
        if not isinstance(data['password'], str) or len(data['password']) > 256 or '\n' in data['password'] or '\r' in data['password']:
            raise HTTPException(422, '无效授权码')
        with self.mail_lock:
            old = self.mail_settings()
            if not data['password']:
                if (data['provider'], data['sender']) != (old['provider'], old['sender']):
                    raise HTTPException(422, '更换发信账号时需要重新填写授权码')
                data['password'] = old['password']
            target = self.path / 'mail.json'
            tmp = self.path / 'mail.json.tmp'
            fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, 'w', encoding='utf-8') as out:
                json.dump(data, out)
                out.flush()
                os.fsync(out.fileno())
            os.replace(tmp, target)
        if data['enabled'] and data['password']:
            with self.db() as db:
                db.execute("UPDATE outbox SET status='pending',next_attempt=0 WHERE status IN ('waiting_config','paused')")
        return self.mail_public()

    def protocol_public(self):
        with self.db() as db:
            row = db.execute('SELECT * FROM device_settings WHERE address=1').fetchone()
        values = dict(zip(PROTOCOL_FIELDS, (row[name] for name in PROTOCOL_FIELDS))) if row else dict(PROTOCOL_DEFAULTS)
        values['token'] = protocol_token(*[values[name] for name in PROTOCOL_FIELDS])
        return values

    def protocol_save(self, data):
        # 严格 4 整数：多键/缺键/非 int（含 bool）/越界一律 422，绝不部分应用。
        if type(data) is not dict or set(data) != set(PROTOCOL_FIELDS):
            raise HTTPException(422, '协议配置必须恰好包含 t_ms、dash_t、word_gap_t、msg_gap_t 四个整数')
        for name, (low, high) in PROTOCOL_LIMITS.items():
            if type(data[name]) is not int or not low <= data[name] <= high:
                raise HTTPException(422, f'无效的协议参数 {name}（允许范围 {low}–{high}）')
        with self.db() as db:
            db.execute('INSERT INTO device_settings(address,t_ms,dash_t,word_gap_t,msg_gap_t,updated_at) VALUES(1,?,?,?,?,?) '
                       'ON CONFLICT(address) DO UPDATE SET t_ms=excluded.t_ms,dash_t=excluded.dash_t,'
                       'word_gap_t=excluded.word_gap_t,msg_gap_t=excluded.msg_gap_t,updated_at=excluded.updated_at',
                       (data['t_ms'], data['dash_t'], data['word_gap_t'], data['msg_gap_t'], utcnow()))
        return self.protocol_public()

    def ingest(self, data, alarm, is_test=False, force_mail=False):
        canonical = json.dumps(data, sort_keys=True, separators=(',', ':'), ensure_ascii=True)
        hashed = digest(canonical + ('|test' if is_test else ''))
        with self.db() as db:
            db.execute('BEGIN IMMEDIATE')
            old = db.execute('SELECT content_hash FROM messages WHERE event_id=?', (data['event_id'],)).fetchone()
            if old:
                if not hmac.compare_digest(old['content_hash'], hashed):
                    raise HTTPException(409, '同一事件编号的内容发生变化')
                return False
            db.execute('''INSERT INTO messages(event_id,content_hash,payload,address,text,alarm,is_test,received_at,ingested_at)
                          VALUES(?,?,?,?,?,?,?,?,?)''', (data['event_id'], hashed, canonical, data['address'], data['text'],
                          int(alarm), int(is_test), data['received_at'], utcnow()))
            if alarm or force_mail:
                settings = self.mail_settings()
                status = 'paused' if not settings['enabled'] else ('pending' if settings['password'] else 'waiting_config')
                db.execute('INSERT INTO outbox(event_id,recipient,status,created_at) VALUES(?,?,?,?)',
                           (data['event_id'], self.config['recipient'], status, utcnow()))
        # Only return to the HTTP handler after both the message and outbox COMMIT.
        return True

    def rows(self, where='1=1', params=(), limit=25, offset=0):
        with self.db() as db:
            total = db.execute('SELECT COUNT(*) FROM messages m WHERE ' + where, params).fetchone()[0]
            records = db.execute('''SELECT m.*,o.status AS mail_status,o.last_error AS mail_error,o.sent_at AS mail_sent_at
                                    FROM messages m LEFT JOIN outbox o ON m.event_id=o.event_id
                                    WHERE ''' + where + ' ORDER BY m.id DESC LIMIT ? OFFSET ?', (*params, limit, offset)).fetchall()
        items = []
        for row in records:
            item = json.loads(row['payload'])
            item.update({key: row[key] for key in ('id', 'ingested_at', 'acknowledged_at', 'mail_status', 'mail_error', 'mail_sent_at')})
            item['alarm'] = bool(row['alarm'])
            item['is_test'] = bool(row['is_test'])
            items.append(item)
        return {'items': items, 'total': total}

    def send_one(self):
        settings = self.mail_settings()
        if not settings.get('enabled') or not settings.get('password'):
            return False
        with self.db() as db:
            db.execute('BEGIN IMMEDIATE')
            item = db.execute("SELECT o.*,m.payload,m.is_test FROM outbox o JOIN messages m USING(event_id) WHERE o.status='pending' AND o.next_attempt<=? ORDER BY o.id LIMIT 1", (time.time(),)).fetchone()
            if item is None:
                return False
            db.execute("UPDATE outbox SET status='sending',attempts=attempts+1 WHERE id=?", (item['id'],))
        data = json.loads(item['payload'])
        mail = EmailMessage()
        mail['From'] = settings['sender']
        mail['To'] = item['recipient']
        mail['Subject'] = ('【LiFi 系统测试】测试数据通知' if item['is_test'] else '【LiFi 求救告警】检测到 ' + data['text'][:40])
        mail['Date'] = formatdate(localtime=False)
        mail['Message-ID'] = '<lifi.' + digest(item['event_id']) + '@lifi.hsn811.top>'
        label = '这是系统测试数据，不代表真实求救。' if item['is_test'] else '接收端已识别到完整求救消息，请核实现场情况。'
        mail.set_content(f"{label}\n\n内容：{data['text']}\n地址编号：{data['address']}\n消息编号：{data['message_id']}\n接收时间：{data['received_at'] or 'RTC 未校时'}\nT：{data['t_ms']} ms\n事件：{data['event_id']}\n\n查看记录：{self.config['origin']}\n")
        smtp = None
        submitted = False
        outcome, error = 'sent', None
        try:
            host = 'smtp.qq.com' if settings['provider'] == 'qq' else 'smtp.resend.com'
            username = settings['sender'] if settings['provider'] == 'qq' else 'resend'
            smtp = smtplib.SMTP_SSL(host, 465, timeout=20, context=ssl.create_default_context())
            smtp.login(username, settings['password'])
            submitted = True
            refused = smtp.send_message(mail)
            if refused:
                outcome, error = 'failed', '收件人被发信服务拒绝'
        except smtplib.SMTPAuthenticationError:
            outcome, error = 'failed', 'SMTP 认证失败，请检查邮箱和授权码'
        except smtplib.SMTPRecipientsRefused:
            outcome, error = 'failed', '收件地址被发信服务拒绝'
        except smtplib.SMTPDataError as exc:
            outcome = 'pending' if 400 <= exc.smtp_code < 500 else 'failed'
            error = '邮件内容未被 SMTP 接受，状态码 ' + str(exc.smtp_code)
        except Exception:
            outcome = 'uncertain' if submitted else 'pending'
            error = 'SMTP 提交时连接中断，结果待确认，未自动重发' if submitted else '未能连接或验证发信服务，将有限重试'
        finally:
            if smtp:
                try:
                    smtp.quit()
                except Exception:
                    smtp.close()
        attempts = item['attempts'] + 1
        if outcome == 'pending' and attempts >= 3:
            outcome = 'failed'
            error = (error or '') + '；已达到 3 次尝试上限'
        with self.db() as db:
            db.execute('UPDATE outbox SET status=?,last_error=?,sent_at=?,next_attempt=? WHERE id=?',
                       (outcome, error, utcnow() if outcome == 'sent' else None, time.time() + 60 * attempts, item['id']))
        return True


def create_app(config=None, worker=True):
    if config is None:
        config = json.loads(Path(os.environ.get('LIFI_CONFIG', '/etc/lifi/config.json')).read_text(encoding='utf-8'))
    store = Store(config)

    @asynccontextmanager
    async def lifespan(app):
        stopped = asyncio.Event()
        async def mail_worker():
            while not stopped.is_set():
                try:
                    await asyncio.to_thread(store.send_one)
                except Exception:
                    # Never print SMTP exception strings: providers can echo credentials.
                    pass
                try:
                    await asyncio.wait_for(stopped.wait(), timeout=10)
                except TimeoutError:
                    pass
        task = asyncio.create_task(mail_worker()) if worker else None
        yield
        stopped.set()
        if task:
            await task

    app = FastAPI(lifespan=lifespan, docs_url=None, redoc_url=None, openapi_url=None)
    app.state.store = store

    @app.middleware('http')
    async def headers(request, call_next):
        response = await call_next(request)
        response.headers['Cache-Control'] = 'no-store'
        response.headers['X-Content-Type-Options'] = 'nosniff'
        response.headers['X-Frame-Options'] = 'DENY'
        response.headers['Referrer-Policy'] = 'no-referrer'
        response.headers['Content-Security-Policy'] = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"
        return response

    async def body(request, limit=8192):
        if request.headers.get('content-type', '').split(';')[0].strip() != 'application/json':
            raise HTTPException(415, '请使用 application/json')
        if request.headers.get('content-encoding', 'identity') != 'identity':
            raise HTTPException(415, '不支持压缩的请求正文')
        content = bytearray()
        async for chunk in request.stream():
            content.extend(chunk)
            if len(content) > limit:
                raise HTTPException(413, '请求正文过大')
        def unique(pairs):
            out = {}
            for k, v in pairs:
                if k in out:
                    raise ValueError('duplicate key')
                out[k] = v
            return out
        try:
            return json.loads(content, object_pairs_hook=unique)
        except (ValueError, UnicodeError):
            raise HTTPException(400, '无效 JSON')

    def check_origin(request):
        origin = request.headers.get('origin')
        if origin is not None and origin != config['origin']:
            raise HTTPException(403, '请求来源不允许')

    def session(request, mutate=False):
        token = request.cookies.get('lifi_session', '')
        with store.db() as db:
            record = db.execute('SELECT * FROM sessions WHERE token_hash=? AND expires>?', (digest(token), time.time())).fetchone()
        if not record:
            raise HTTPException(401, '请先登录')
        if mutate:
            check_origin(request)
            if not hmac.compare_digest(request.headers.get('x-csrf-token', ''), record['csrf']):
                raise HTTPException(403, '页面验证已失效，请刷新后重试')
        return record

    @app.get('/api/health')
    def health():
        with store.db() as db:
            db.execute('SELECT 1').fetchone()
        return {'ok': True, 'version': VERSION}

    @app.post('/api/v1/messages')
    async def ingest(request: Request):
        auth = request.headers.get('authorization', '')
        hashed = digest(auth[7:]) if auth.startswith('Bearer ') else ''
        address = next((int(a) for a, token in config['device_tokens'].items() if hmac.compare_digest(hashed, token)), None)
        if address is None:
            raise HTTPException(401, '设备凭据无效')
        data = await body(request, 2048)
        alarm = validate_message(data, address, request.headers.get('idempotency-key'))
        is_test = address in config.get('test_addresses', [])
        created = store.ingest(data, alarm, is_test=is_test)
        return JSONResponse({'ok': True, 'event_id': data['event_id'], 'duplicate': not created, 'alarm': alarm},
                            status_code=201 if created else 200, headers={'X-Morse-Ack': data['event_id']})

    @app.post('/api/auth/login')
    async def login(request: Request):
        check_origin(request)
        data = await body(request, 1024)
        attempt = store.login_attempts['admin']
        now = time.time()
        while attempt and attempt[0] < now - 600:
            attempt.popleft()
        if len(attempt) >= 15:
            raise HTTPException(429, '尝试次数较多，请 10 分钟后重试')
        attempt.append(now)
        if type(data) is not dict or not isinstance(data.get('password'), str) or not check_password(data['password'], config['admin_password_hash']):
            raise HTTPException(401, '管理密码不正确')
        attempt.clear()
        token, csrf = secrets.token_urlsafe(32), secrets.token_urlsafe(24)
        with store.db() as db:
            db.execute('DELETE FROM sessions WHERE expires<?', (now,))
            db.execute('INSERT INTO sessions VALUES(?,?,?)', (digest(token), csrf, now + 43200))
        response = JSONResponse({'ok': True, 'csrf': csrf})
        response.set_cookie('lifi_session', token, max_age=43200, secure=config.get('secure_cookie', True), httponly=True, samesite='strict')
        return response

    @app.get('/api/auth/session')
    def current(request: Request):
        s = session(request)
        return {'ok': True, 'csrf': s['csrf']}

    @app.post('/api/auth/logout')
    def logout(request: Request):
        s = session(request, True)
        with store.db() as db:
            db.execute('DELETE FROM sessions WHERE token_hash=?', (s['token_hash'],))
        response = JSONResponse({'ok': True})
        response.delete_cookie('lifi_session')
        return response

    @app.get('/api/dashboard')
    def dashboard(request: Request):
        session(request)
        with store.db() as db:
            stats = dict(db.execute('''SELECT COUNT(*) AS total,COALESCE(SUM(alarm=1 AND acknowledged_at IS NULL),0) AS pending_alarms,
                                       COALESCE(SUM(is_test=1),0) AS test_messages FROM messages''').fetchone())
            stats['mail_sent'] = db.execute("SELECT COUNT(*) FROM outbox WHERE status='sent'").fetchone()[0]
            devices = [dict(row) for row in db.execute('''SELECT address,COUNT(*) AS messages,MAX(ingested_at) AS last_seen FROM messages WHERE is_test=0 GROUP BY address''')]
            recent_mail = [dict(row) for row in db.execute('SELECT id,event_id,status,attempts,last_error,sent_at,created_at FROM outbox ORDER BY id DESC LIMIT 8')]
        known = {d['address'] for d in devices}
        devices += [{'address': int(a), 'messages': 0, 'last_seen': None} for a in config['device_tokens'] if int(a) not in known and int(a) not in config.get('test_addresses', [])]
        return {'stats': stats, 'devices': devices, 'mail': store.mail_public(), 'outbox': recent_mail, 'server_time': utcnow(), 'version': VERSION}

    @app.get('/api/messages')
    def messages(request: Request, q: str = '', kind: str = 'all', page: int = 1):
        session(request)
        if kind not in ('all', 'alarm', 'normal', 'test') or page < 1 or page > 100000 or len(q) > 96:
            raise HTTPException(422, '无效筛选条件')
        where, values = ['1=1'], []
        if q:
            where.append('instr(m.text,?)>0')
            values.append(q.upper())
        if kind == 'alarm':
            where.append('m.alarm=1')
        elif kind == 'normal':
            where.append('m.alarm=0 AND m.is_test=0')
        elif kind == 'test':
            where.append('m.is_test=1')
        return store.rows(' AND '.join(where), tuple(values), offset=(page - 1) * 25) | {'page': page, 'page_size': 25}

    @app.post('/api/messages/{record_id}/acknowledge')
    def acknowledge(record_id: int, request: Request):
        session(request, True)
        with store.db() as db:
            found = db.execute('SELECT alarm FROM messages WHERE id=?', (record_id,)).fetchone()
            if not found:
                raise HTTPException(404, '消息不存在')
            if not found['alarm']:
                raise HTTPException(409, '这条消息不是求救告警')
            db.execute('UPDATE messages SET acknowledged_at=COALESCE(acknowledged_at,?) WHERE id=?', (utcnow(), record_id))
        return {'ok': True}

    @app.post('/api/settings/mail')
    async def save_mail(request: Request):
        session(request, True)
        return store.save_mail(await body(request, 2048))

    @app.post('/api/test-email')
    async def test_email(request: Request):
        session(request, True)
        data = await body(request, 1024)
        key = data.get('request_id') if type(data) is dict else None
        if not isinstance(key, str) or not re.fullmatch(r'[a-zA-Z0-9_-]{8,80}', key):
            raise HTTPException(422, '无效测试请求编号')
        if not store.mail_public()['configured'] or not store.mail_public()['enabled']:
            raise HTTPException(409, '请先保存并启用发信授权码')
        # Same test button/network retry -> same durable message and mail task.
        event = {'schema': 1, 'event_id': 'test-' + key, 'address': 1, 'boot': 0, 'message_id': 1,
                 'uptime_ms': 0, 't_ms': 60, 'time_valid': False, 'received_at': None,
                 'boot_counter_valid': False, 'decode_valid': True, 'truncated': False, 'alarm': False, 'text': 'LIFI TEST OK'}
        with store.db() as db:
            exists = db.execute('SELECT 1 FROM messages WHERE event_id=?', (event['event_id'],)).fetchone()
            last = db.execute('SELECT created_at FROM outbox ORDER BY id DESC LIMIT 1').fetchone()
        if not exists and last and (datetime.now(timezone.utc) - datetime.fromisoformat(last[0])).total_seconds() < 60:
            raise HTTPException(429, '请等待 60 秒后再发送测试邮件')
        created = store.ingest(event, False, is_test=True, force_mail=True)
        return {'ok': True, 'event_id': event['event_id'], 'duplicate': not created, 'status': 'queued'}

    @app.get('/app/api/protocol')
    def protocol_read():
        # 手机 App 启动时拉取当前四元组与一致性令牌（公开，只读）。
        return store.protocol_public()

    @app.put('/app/api/protocol')
    async def protocol_write(request: Request):
        # App 直连写入：严格 4 整数校验后落库，返回新令牌供 App 本地比对。
        return store.protocol_save(await body(request, 1024))

    @app.get('/')
    def index(request: Request):
        # 同一服务器托管两个前端：管理站（默认）与手机 App（lifiapp.jjqpro.com，nginx 已传正确 Host）。
        host = (request.headers.get('host') or '').split(':')[0].lower()
        page = 'lifapp/index.html' if host == 'lifiapp.jjqpro.com' else 'index.html'
        return FileResponse(HERE / 'static' / page)

    app.mount('/static', StaticFiles(directory=HERE / 'static'), name='static')
    app.mount('/app/static', StaticFiles(directory=HERE / 'static' / 'lifapp'), name='app-static')
    return app

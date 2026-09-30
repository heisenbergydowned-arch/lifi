"""Explicit deploy smoke test: one API test row and one idempotent test email."""
import json
from pathlib import Path
import sys
from datetime import datetime, timedelta, timezone
import httpx

ROOT = Path(__file__).resolve().parents[1]
credentials = json.loads((ROOT / '.private/verification.json').read_text())
if '--send-initial-test' in sys.argv and not credentials.get('test_token'):
    raise SystemExit('本次接口联调凭据已撤销；使用默认只读检查，或另行配置专用测试设备凭据。')
origin = 'https://lifi.hsn811.top'
report = {}
with httpx.Client(base_url=origin, timeout=30, follow_redirects=False, trust_env=False) as c:
    health = c.get('/api/health'); health.raise_for_status()
    report['health'] = health.json()
    assert c.get('/api/dashboard').status_code == 401
    report['unauthenticated_dashboard'] = 401
    login = c.post('/api/auth/login', json={'password': credentials['admin_password']}); login.raise_for_status()
    c.headers['X-CSRF-Token'] = login.json()['csrf']
    c.headers['Origin'] = origin
    if '--send-initial-test' in sys.argv:
        data = {'schema': 1, 'event_id': 'deploy-api-check-20260910-1', 'address': 65535, 'boot': 1,
                'message_id': 1, 'uptime_ms': 1000, 't_ms': 60, 'time_valid': False, 'received_at': None,
                'boot_counter_valid': False, 'decode_valid': True, 'truncated': False, 'alarm': False, 'text': 'API CHECK'}
        headers = {'Authorization': 'Bearer ' + credentials['test_token'], 'Idempotency-Key': data['event_id']}
        first = c.post('/api/v1/messages', json=data, headers=headers)
        assert first.status_code in (200, 201), first.status_code
        assert first.headers.get('X-Morse-Ack') == data['event_id']
        retry = c.post('/api/v1/messages', json=data, headers=headers)
        assert retry.status_code == 200 and retry.json()['duplicate']
        assert retry.headers.get('X-Morse-Ack') == data['event_id']
        changed = dict(data, text='DIFFERENT')
        conflict = c.post('/api/v1/messages', json=changed, headers=headers)
        assert conflict.status_code == 409 and 'X-Morse-Ack' not in conflict.headers
        report['ingest'] = {'first': first.status_code, 'retry': retry.status_code, 'conflict': conflict.status_code, 'ack_verified': True}
        test = c.post('/api/test-email', json={'request_id': 'initial-test-20260910-one-email'})
        test.raise_for_status()
        report['test'] = test.json()
    dashboard = c.get('/api/dashboard'); dashboard.raise_for_status()
    report['dashboard'] = dashboard.json()
    messages = c.get('/api/messages?kind=test'); messages.raise_for_status()
    report['test_records'] = messages.json()
    report['verified_at'] = datetime.now(timezone.utc).isoformat()
    c.post('/api/auth/logout')
path = ROOT.parent / 'Docs/verification' / ('lifi-live-ingest.json' if '--send-initial-test' in sys.argv else 'lifi-live-status.json')
path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False, indent=2))

import copy
import json
import smtplib
import sys
import tempfile
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from unittest.mock import patch, MagicMock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from fastapi.testclient import TestClient
from server import PROTOCOL_DEFAULTS, Store, create_app, digest, password_hash, protocol_token

ADMIN_HASH = password_hash('test-admin-not-production')


def message(text='HELLO', event='a1-b4-m1-test'):
    return {'schema': 1, 'event_id': event, 'address': 1, 'boot': 4, 'message_id': 1,
            'uptime_ms': 64000, 't_ms': 60, 'time_valid': True,
            'received_at': '2026-09-10T20:01:00+08:00', 'boot_counter_valid': True,
            'decode_valid': True, 'truncated': False, 'alarm': False, 'text': text}


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.config = {'data_dir': self.temp.name, 'origin': 'https://testserver',
                       'recipient': 'recipient@example.com', 'admin_password_hash': ADMIN_HASH,
                       'device_tokens': {'1': digest('test-device-token')}}
        self.app = create_app(self.config, worker=False)
        self.store = self.app.state.store
        self.client = TestClient(self.app, base_url='https://testserver')

    def tearDown(self):
        self.client.close()
        self.temp.cleanup()

    def post_message(self, data):
        return self.client.post('/api/v1/messages', json=data, headers={
            'Authorization': 'Bearer test-device-token', 'Idempotency-Key': data['event_id']})

    def login(self):
        response = self.client.post('/api/auth/login', json={'password': 'test-admin-not-production'})
        self.assertEqual(response.status_code, 200)
        return {'X-CSRF-Token': response.json()['csrf'], 'Origin': self.config['origin']}

    def configure_mail(self):
        self.store.save_mail({'provider': 'qq', 'sender': '123456@qq.com', 'password': 'test-secret', 'enabled': True})

    def test_health_public_data_private(self):
        self.assertEqual(self.client.get('/api/health').status_code, 200)
        self.assertEqual(self.client.get('/api/dashboard').status_code, 401)
        self.assertEqual(self.client.get('/api/messages').status_code, 401)
        self.assertEqual(self.client.get('/openapi.json').status_code, 404)

    def test_api_unauthorized_no_ack(self):
        response = self.client.post('/api/v1/messages', json=message())
        self.assertEqual(response.status_code, 401)
        self.assertNotIn('X-Morse-Ack', response.headers)

    def test_persistent_ack_and_idempotency(self):
        data = message('SOS')
        self.assertEqual(self.post_message(data).status_code, 201)
        self.assertEqual(self.post_message(data).status_code, 200)
        response = self.post_message(data)
        self.assertEqual(response.headers['X-Morse-Ack'], data['event_id'])
        reopened = Store(self.config)
        with reopened.db() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM messages').fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT COUNT(*) FROM outbox').fetchone()[0], 1)

    def test_concurrent_retry_commits_once(self):
        with ThreadPoolExecutor(max_workers=8) as pool:
            outcomes = list(pool.map(lambda _: self.store.ingest(message('SOS'), True), range(16)))
        self.assertEqual(outcomes.count(True), 1)
        with self.store.db() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM outbox').fetchone()[0], 1)

    def test_conflicting_reuse_no_ack(self):
        self.post_message(message())
        response = self.post_message(message('SOS'))
        self.assertEqual(response.status_code, 409)
        self.assertNotIn('X-Morse-Ack', response.headers)

    def test_storage_failure_never_acknowledges(self):
        with patch.object(self.store, 'ingest', side_effect=RuntimeError('simulated disk failure')):
            with TestClient(self.app, base_url='https://testserver', raise_server_exceptions=False) as client:
                r = client.post('/api/v1/messages', json=message(), headers={'Authorization': 'Bearer test-device-token', 'Idempotency-Key': message()['event_id']})
                self.assertEqual(r.status_code, 500)
                self.assertNotIn('X-Morse-Ack', r.headers)

    def test_alarm_recomputed_and_word_boundaries(self):
        for i, (text, expected) in enumerate([('SOS', True), ('PLEASE HELP', True), ('HELP?', True), ('SOS1', False), ('HELPER', False), ('HELLO', False)]):
            data = message(text, 'case-' + str(i)); data['alarm'] = not expected
            self.assertEqual(self.post_message(data).json()['alarm'], expected)

    def test_invalid_or_truncated_never_alarms(self):
        for field in ('decode_valid', 'truncated'):
            data = message('SOS', 'invalid-' + field)
            data[field] = field == 'truncated'
            self.assertFalse(self.post_message(data).json()['alarm'])
        with self.store.db() as db:
            self.assertEqual(db.execute('SELECT COUNT(*) FROM outbox').fetchone()[0], 0)

    def test_strict_fields_time_types_and_key(self):
        mutations = [('address', 2, 403), ('message_id', True, 422), ('time_valid', 1, 422),
                     ('received_at', '2026-02-30T00:00:00+08:00', 422), ('text', '<script>', 422),
                     ('text', 'X' * 97, 422), ('t_ms', 0, 422), ('event_id', 'bad\r\nHeader', 422)]
        for key, value, expected in mutations:
            with self.subTest(key=key, value=value):
                data = message(); data[key] = value
                if key == 'event_id':
                    r = self.client.post('/api/v1/messages', json=data, headers={'Authorization': 'Bearer test-device-token', 'Idempotency-Key': 'different'})
                else:
                    r = self.post_message(data)
                self.assertEqual(r.status_code, expected)
                self.assertNotIn('X-Morse-Ack', r.headers)

    def test_uncalibrated_clock_null_supported(self):
        data = message(); data['time_valid'] = False; data['received_at'] = None
        self.assertEqual(self.post_message(data).status_code, 201)

    def test_oversize_and_duplicate_json_keys(self):
        headers = {'Authorization': 'Bearer test-device-token', 'Content-Type': 'application/json'}
        self.assertEqual(self.client.post('/api/v1/messages', content='x' * 3000, headers=headers).status_code, 413)
        self.assertEqual(self.client.post('/api/v1/messages', content='{"schema":1,"schema":2}', headers=headers).status_code, 400)

    def test_session_cookie_and_csrf(self):
        headers = self.login()
        self.assertIn('lifi_session', self.client.cookies)
        self.assertEqual(self.client.get('/api/dashboard').status_code, 200)
        self.assertEqual(self.client.post('/api/auth/logout').status_code, 403)
        bad = headers | {'Origin': 'https://untrusted.example'}
        self.assertEqual(self.client.post('/api/auth/logout', headers=bad).status_code, 403)
        self.assertEqual(self.client.post('/api/auth/logout', headers=headers).status_code, 200)
        self.assertEqual(self.client.get('/api/dashboard').status_code, 401)

    def test_mail_secret_not_returned_and_blank_preserves(self):
        self.configure_mail()
        headers = self.login()
        response = self.client.get('/api/dashboard')
        self.assertNotIn('test-secret', response.text)
        saved = self.client.post('/api/settings/mail', headers=headers, json={'provider': 'qq', 'sender': '123456@qq.com', 'password': '', 'enabled': True})
        self.assertEqual(saved.status_code, 200)
        self.assertNotIn('test-secret', saved.text)
        self.assertEqual(self.store.mail_settings()['password'], 'test-secret')

    def test_test_mail_single_request_single_record(self):
        self.configure_mail(); headers = self.login()
        first = self.client.post('/api/test-email', json={'request_id': 'one-test-12345'}, headers=headers)
        retry = self.client.post('/api/test-email', json={'request_id': 'one-test-12345'}, headers=headers)
        self.assertEqual(first.status_code, 200)
        self.assertTrue(retry.json()['duplicate'])
        with self.store.db() as db:
            self.assertEqual(tuple(db.execute('SELECT is_test,alarm,text FROM messages').fetchone()), (1, 0, 'LIFI TEST OK'))
            self.assertEqual(db.execute('SELECT COUNT(*) FROM outbox').fetchone()[0], 1)

    def test_test_mail_requires_configuration(self):
        headers = self.login()
        self.assertEqual(self.client.post('/api/test-email', json={'request_id': 'test-12345'}, headers=headers).status_code, 409)

    def test_filter_and_acknowledge(self):
        self.post_message(message('SOS')); self.post_message(message('HELLO', 'second'))
        headers = self.login()
        data = self.client.get('/api/messages?kind=alarm').json()
        self.assertEqual(data['total'], 1)
        rid = data['items'][0]['id']
        self.assertEqual(self.client.post(f'/api/messages/{rid}/acknowledge', headers=headers).status_code, 200)
        self.assertEqual(self.client.get('/api/dashboard').json()['stats']['pending_alarms'], 0)
        self.assertEqual(self.client.get('/api/messages?q=HEL').json()['total'], 1)

    def queue_mail(self):
        self.configure_mail(); self.store.ingest(message('SOS'), True)

    def mail_status(self):
        with self.store.db() as db:
            return db.execute('SELECT status FROM outbox').fetchone()[0]

    @patch('server.smtplib.SMTP_SSL')
    def test_smtp_accept_once_even_if_quit_fails(self, cls):
        self.queue_mail(); smtp = cls.return_value
        smtp.send_message.return_value = {}; smtp.quit.side_effect = OSError('quit disconnected')
        self.assertTrue(self.store.send_one()); self.assertEqual(self.mail_status(), 'sent')
        self.assertFalse(self.store.send_one()); self.assertEqual(smtp.send_message.call_count, 1)
        mail = smtp.send_message.call_args.args[0]
        self.assertEqual(mail['To'], 'recipient@example.com')
        self.assertIsNotNone(cls.call_args.kwargs['context'])

    @patch('server.smtplib.SMTP_SSL')
    def test_disconnect_during_data_is_uncertain_no_retry(self, cls):
        self.queue_mail(); cls.return_value.send_message.side_effect = smtplib.SMTPServerDisconnected('test')
        self.store.send_one(); self.assertEqual(self.mail_status(), 'uncertain')
        self.assertFalse(self.store.send_one())

    @patch('server.smtplib.SMTP_SSL')
    def test_auth_failure_no_credential_in_error(self, cls):
        self.queue_mail(); cls.return_value.login.side_effect = smtplib.SMTPAuthenticationError(535, b'test-secret')
        self.store.send_one(); self.assertEqual(self.mail_status(), 'failed')
        with self.store.db() as db:
            self.assertNotIn('test-secret', db.execute('SELECT last_error FROM outbox').fetchone()[0])

    def test_restart_during_send_requires_review(self):
        self.queue_mail()
        with self.store.db() as db:
            db.execute("UPDATE outbox SET status='sending'")
        Store(self.config)
        self.assertEqual(self.mail_status(), 'uncertain')

    def test_test_address_is_labeled_and_excluded_from_devices(self):
        self.config['test_addresses'] = [1]
        self.post_message(message())
        self.login()
        self.assertTrue(self.client.get('/api/messages').json()['items'][0]['is_test'])
        self.assertEqual(self.client.get('/api/dashboard').json()['devices'], [])

    def protocol(self):
        return self.client.get('/app/api/protocol').json()

    def test_protocol_token_function_matches_fixed_vectors(self):
        self.assertEqual(protocol_token(60, 3, 4, 10), 'e815b5c76073af05')
        self.assertEqual(protocol_token(80, 4, 5, 12), 'd2c417a2abb16cb7')

    def test_protocol_get_defaults_without_auth(self):
        # App 启动时公开拉取：默认四元组 + 固定令牌 e815b5c76073af05。
        self.assertEqual(self.protocol(), {'t_ms': 60, 'dash_t': 3, 'word_gap_t': 4, 'msg_gap_t': 10, 'token': 'e815b5c76073af05'})

    def test_protocol_put_updates_token_and_persists(self):
        response = self.client.put('/app/api/protocol', json={'t_ms': 80, 'dash_t': 4, 'word_gap_t': 5, 'msg_gap_t': 12})
        self.assertEqual(response.status_code, 200)
        data = response.json()
        self.assertEqual((data['t_ms'], data['dash_t'], data['word_gap_t'], data['msg_gap_t']), (80, 4, 5, 12))
        self.assertEqual(data['token'], 'd2c417a2abb16cb7')
        self.assertEqual(self.protocol()['token'], 'd2c417a2abb16cb7')
        reopened = Store(self.config)
        self.assertEqual(reopened.protocol_public()['token'], 'd2c417a2abb16cb7')

    def test_protocol_put_boundaries(self):
        for values in ({'t_ms': 60, 'dash_t': 2, 'word_gap_t': 2, 'msg_gap_t': 4},
                       {'t_ms': 2000, 'dash_t': 5, 'word_gap_t': 8, 'msg_gap_t': 20}):
            with self.subTest(values=values):
                self.assertEqual(self.client.put('/app/api/protocol', json=values).status_code, 200)
        for key, bad in (('t_ms', 0), ('t_ms', 59), ('t_ms', 2001), ('t_ms', 100000),
                         ('dash_t', 1), ('dash_t', 6), ('word_gap_t', 1), ('word_gap_t', 9),
                         ('msg_gap_t', 3), ('msg_gap_t', 21)):
            with self.subTest(key=key, value=bad):
                values = dict(PROTOCOL_DEFAULTS); values[key] = bad
                self.assertEqual(self.client.put('/app/api/protocol', json=values).status_code, 422)
        # 被拒绝的写入绝不改动已存配置。
        self.assertEqual((self.protocol()['t_ms'], self.protocol()['dash_t'], self.protocol()['word_gap_t'], self.protocol()['msg_gap_t']),
                         (2000, 5, 8, 20))

    def test_protocol_put_invalid_bodies(self):
        bad = dict(PROTOCOL_DEFAULTS); del bad['dash_t']
        extra = dict(PROTOCOL_DEFAULTS); extra['extra'] = 1
        for values in (bad, extra,
                       dict(PROTOCOL_DEFAULTS, t_ms='60'),
                       dict(PROTOCOL_DEFAULTS, dash_t=True),
                       dict(PROTOCOL_DEFAULTS, word_gap_t=4.0),
                       dict(PROTOCOL_DEFAULTS, msg_gap_t=None),
                       [60, 3, 4, 10]):
            with self.subTest(values=values):
                self.assertEqual(self.client.put('/app/api/protocol', json=values).status_code, 422)
        json_headers = {'Content-Type': 'application/json'}
        self.assertEqual(self.client.put('/app/api/protocol', content='t_ms=80', headers={'Content-Type': 'text/plain'}).status_code, 415)
        self.assertEqual(self.client.put('/app/api/protocol', content='{"t_ms":60,"t_ms":60,"dash_t":3,"word_gap_t":4,"msg_gap_t":10}',
                                         headers=json_headers).status_code, 400)
        # 默认配置保持不变。
        self.assertEqual(self.protocol()['token'], 'e815b5c76073af05')

    def test_root_dispatches_by_host(self):
        # 管理站（默认 Host）与手机 App（lifiapp.jjqpro.com，nginx 已传正确 Host）互不串扰。
        admin = self.client.get('/').text
        self.assertNotIn('LiFi 发送', admin)
        for host in ('lifiapp.jjqpro.com', 'lifiapp.jjqpro.com:8444'):
            with self.subTest(host=host):
                app_page = self.client.get('/', headers={'Host': host}).text
                self.assertIn('LiFi 发送', app_page)
                self.assertIn('/app/static/lifapp.js', app_page)

    def test_app_static_files_served(self):
        response = self.client.get('/app/static/lifapp.css')
        self.assertEqual(response.status_code, 200)
        self.assertIn('text/css', response.headers['content-type'])


if __name__ == '__main__':
    unittest.main(verbosity=2)

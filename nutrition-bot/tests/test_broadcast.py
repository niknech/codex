import importlib.util
from pathlib import Path
import sqlite3
import unittest

spec = importlib.util.spec_from_file_location('broadcast', Path(__file__).parents[1] / 'deploy/broadcast.py')
broadcast = importlib.util.module_from_spec(spec)
spec.loader.exec_module(broadcast)


class BroadcastTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(':memory:')
        self.addCleanup(self.db.close)

    def run_send(self, transport, text='hello', chats=(1, 2)):
        return broadcast.run_campaign(self.db, 'test', text, chats, transport, lambda _: None)

    def test_resume_and_frozen_recipients(self):
        calls = []
        def send(chat, text):
            calls.append(chat)
            return {'ok': True}
        self.run_send(send)
        self.run_send(send, chats=(1, 2, 3))
        self.assertEqual(calls, [1, 2])
        with self.assertRaises(ValueError):
            self.run_send(send, text='changed')

    def test_unknown_not_retried(self):
        def failure(chat, text):
            raise TimeoutError('secret must not be printed')
        self.assertEqual(self.run_send(failure), {'pending': 1, 'unknown': 1})
        calls = []
        self.run_send(lambda chat, text: calls.append(chat) or {'ok': True})
        self.assertEqual(calls, [2])

    def test_rate_limit_and_blocked(self):
        result = self.run_send(lambda *_: {'ok': False, 'error_code': 429})
        self.assertEqual(result, {'pending': 2})
        result = self.run_send(lambda chat, _: {'ok': chat == 2, 'error_code': 403})
        self.assertEqual(result, {'failed': 1, 'sent': 1})


if __name__ == '__main__':
    unittest.main()

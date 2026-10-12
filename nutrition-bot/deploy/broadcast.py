#!/usr/bin/env python3
"""Manual Linux admin broadcast. Preview by default; no third-party dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import time
import urllib.error
import urllib.request


def recipients(path):
    with sqlite3.connect(Path(path).resolve().as_uri() + '?mode=ro', uri=True) as db:
        return [row[0] for row in db.execute('SELECT DISTINCT chat FROM users WHERE chat>0 ORDER BY chat')]


def send(token, chat, text):
    request = urllib.request.Request(
        f'https://api.telegram.org/bot{token}/sendMessage',
        data=json.dumps({'chat_id': chat, 'text': text,
                         'link_preview_options': {'is_disabled': True}}).encode(),
        headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        return json.loads(error.read())


def run_campaign(db, campaign, text, chats, transport, pause=time.sleep):
    digest = hashlib.sha256(text.encode()).hexdigest()
    db.executescript('''
        CREATE TABLE IF NOT EXISTS campaigns(id TEXT PRIMARY KEY, digest TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS deliveries(campaign TEXT, chat INTEGER, status TEXT,
            PRIMARY KEY(campaign,chat));
    ''')
    with db:
        previous = db.execute('SELECT digest FROM campaigns WHERE id=?', (campaign,)).fetchone()
        if previous and previous[0] != digest:
            raise ValueError('Этот ID уже использован с другим текстом. Выбери новый ID.')
        if not previous:
            db.execute('INSERT INTO campaigns VALUES(?,?)', (campaign, digest))
            db.executemany('INSERT INTO deliveries VALUES(?,?,?)',
                           [(campaign, chat, 'pending') for chat in chats])
    # Recipient snapshot is frozen on first send. Never automatically retry ambiguous sends.
    for (chat,) in db.execute(
            "SELECT chat FROM deliveries WHERE campaign=? AND status='pending' ORDER BY chat",
            (campaign,)).fetchall():
        with db:
            db.execute("UPDATE deliveries SET status='unknown' WHERE campaign=? AND chat=?",
                       (campaign, chat))
        try:
            result = transport(chat, text)
        except Exception:
            print('Сетевой сбой: доставка неизвестна. Рассылка остановлена; токен не выводится.')
            break
        if result.get('ok') is True:
            status = 'sent'
        elif result.get('error_code') == 429:
            with db:
                db.execute("UPDATE deliveries SET status='pending' WHERE campaign=? AND chat=?",
                           (campaign, chat))
            retry = result.get('parameters', {}).get('retry_after', 60)
            print(f'Лимит Telegram. Повтори ту же команду не раньше чем через {retry} секунд.')
            break
        elif result.get('error_code') in (400, 403):
            status = 'failed'
        else:
            print('Ошибка Telegram: рассылка остановлена. Неясные доставки не повторяются.')
            break
        with db:
            db.execute('UPDATE deliveries SET status=? WHERE campaign=? AND chat=?',
                       (status, campaign, chat))
        pause(1)
    counts = dict(db.execute('SELECT status,COUNT(*) FROM deliveries WHERE campaign=? GROUP BY status',
                            (campaign,)))
    print('Итог:', counts)
    return counts


def main():
    parser = argparse.ArgumentParser(description='Рассылка от имени бота (по умолчанию только просмотр)')
    parser.add_argument('message', type=Path, help='UTF-8 файл с текстом, эмодзи и переносами строк')
    parser.add_argument('--id', required=True, help='Уникальное имя рассылки, например news-2026-10-12')
    parser.add_argument('--send', action='store_true', help='Отправить после подтверждения')
    parser.add_argument('--db', default='/var/lib/nutrition-bot/bot.sqlite3')
    parser.add_argument('--token-file', type=Path, default=Path('/etc/nutrition-bot/telegram_token'))
    parser.add_argument('--state-dir', type=Path, default=Path('/var/lib/nutrition-broadcast'))
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}', args.id):
        raise ValueError('ID: 1–80 букв латиницы, цифр, дефисов или подчёркиваний.')
    text = args.message.read_text(encoding='utf-8-sig').strip()
    if not text or len(text.encode('utf-16-le')) // 2 > 4096:
        raise ValueError('Текст пустой или длиннее 4096 символов (эмодзи могут занимать два).')
    chats = recipients(args.db)
    print(f'Пользователей в базе: {len(chats)}\nID рассылки: {args.id}\n\n{text}\n')
    if not args.send:
        print('Это предпросмотр. Для отправки добавь --send.')
        return
    if input('Для отправки введи ОТПРАВИТЬ: ').strip() != 'ОТПРАВИТЬ':
        print('Отменено.')
        return
    import fcntl  # Linux server: prevent concurrent broadcast processes.
    os.umask(0o077)
    args.state_dir.mkdir(parents=True, exist_ok=True, mode=0o700)
    with (args.state_dir / 'lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        token = args.token_file.read_text().strip()
        if not re.fullmatch(r'\d+:[A-Za-z0-9_-]+', token):
            raise ValueError('Неверный формат файла токена.')
        with sqlite3.connect(args.state_dir / 'history.sqlite3') as db:
            run_campaign(db, args.id, text, chats, lambda chat, body: send(token, chat, body))


if __name__ == '__main__':
    try:
        main()
    except (Exception, KeyboardInterrupt) as error:
        # Never print network exceptions: their URL can contain the bot token.
        print(str(error) if isinstance(error, ValueError) else
              'Остановлено. Проверь пути, права и отсутствие другой запущенной рассылки.')
        raise SystemExit(1)

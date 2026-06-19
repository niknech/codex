from __future__ import annotations

import json
from datetime import datetime, timedelta, timezone
from telethon import TelegramClient
from .config import TELEGRAM_API_ID, TELEGRAM_API_HASH, TELEGRAM_PHONE, TELEGRAM_CHANNEL_USERNAME
from .db import get_connection, init_db, insert_raw_message

async def export_last_year() -> None:
    if not TELEGRAM_API_ID or not TELEGRAM_API_HASH or not TELEGRAM_PHONE:
        raise RuntimeError('Заполните TELEGRAM_API_ID, TELEGRAM_API_HASH и TELEGRAM_PHONE в .env')
    init_db()
    channel = TELEGRAM_CHANNEL_USERNAME
    cutoff = datetime.now(timezone.utc) - timedelta(days=365)
    saved = skipped = 0
    async with TelegramClient('telegram_alert_analyzer', int(TELEGRAM_API_ID), TELEGRAM_API_HASH) as client:
        await client.start(phone=TELEGRAM_PHONE)
        entity = await client.get_entity(channel)
        username = channel.lstrip('@')
        with get_connection() as conn:
            async for msg in client.iter_messages(entity):
                if msg.date < cutoff:
                    break
                if not msg.message:
                    skipped += 1; continue
                item = {
                    'channel_username': channel,
                    'telegram_message_id': msg.id,
                    'message_date': msg.date.isoformat(),
                    'message_text': msg.message,
                    'message_url': f'https://t.me/{username}/{msg.id}',
                    'views': getattr(msg, 'views', None),
                    'raw_json': json.dumps(msg.to_dict(), default=str, ensure_ascii=False),
                }
                saved += int(insert_raw_message(conn, item))
                if (saved + skipped) % 100 == 0:
                    print(f'Обработано: {saved + skipped}, новых: {saved}, пустых: {skipped}')
            conn.commit()
    print(f'Готово. Новых сообщений: {saved}. Пустых пропущено: {skipped}.')

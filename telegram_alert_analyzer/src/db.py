from __future__ import annotations

import sqlite3
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, Optional

from .config import sqlite_path

SCHEMA = '''
CREATE TABLE IF NOT EXISTS raw_messages (
    id INTEGER PRIMARY KEY,
    channel_username TEXT,
    telegram_message_id INTEGER UNIQUE,
    message_date TEXT,
    message_text TEXT,
    message_url TEXT,
    views INTEGER NULL,
    raw_json TEXT NULL,
    created_at TEXT
);
CREATE TABLE IF NOT EXISTS parsed_alerts (
    id INTEGER PRIMARY KEY,
    raw_message_id INTEGER,
    event_date TEXT,
    event_time TEXT,
    region TEXT NULL,
    city TEXT NULL,
    settlement TEXT NULL,
    alert_type TEXT,
    weapon_type TEXT NULL,
    direction TEXT NULL,
    status TEXT,
    confidence REAL,
    parser_version TEXT,
    parse_notes TEXT NULL,
    UNIQUE(raw_message_id, parser_version),
    FOREIGN KEY(raw_message_id) REFERENCES raw_messages(id)
);
CREATE TABLE IF NOT EXISTS message_parse_errors (
    id INTEGER PRIMARY KEY,
    raw_message_id INTEGER,
    error_type TEXT,
    error_text TEXT,
    created_at TEXT,
    FOREIGN KEY(raw_message_id) REFERENCES raw_messages(id)
);
'''

def get_connection(db_path: Optional[Path] = None) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path or sqlite_path())
    conn.row_factory = sqlite3.Row
    return conn

def init_db() -> None:
    path = sqlite_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    with get_connection(path) as conn:
        conn.executescript(SCHEMA)

def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()

def insert_raw_message(conn: sqlite3.Connection, item: dict) -> bool:
    cur = conn.execute('''INSERT OR IGNORE INTO raw_messages
        (channel_username, telegram_message_id, message_date, message_text, message_url, views, raw_json, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)''', (
        item['channel_username'], item['telegram_message_id'], item['message_date'], item['message_text'],
        item['message_url'], item.get('views'), item.get('raw_json'), utc_now()))
    return cur.rowcount > 0

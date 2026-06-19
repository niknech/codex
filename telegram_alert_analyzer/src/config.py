from __future__ import annotations

import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parents[1]

def load_env_file(path: Path) -> None:
    if not path.exists():
        return
    for line in path.read_text(encoding='utf-8').splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, value = line.split('=', 1)
        os.environ.setdefault(key.strip(), value.strip().strip('\"').strip("'"))

load_env_file(BASE_DIR / '.env')

TELEGRAM_API_ID = os.getenv('TELEGRAM_API_ID')
TELEGRAM_API_HASH = os.getenv('TELEGRAM_API_HASH')
TELEGRAM_PHONE = os.getenv('TELEGRAM_PHONE')
TELEGRAM_CHANNEL_USERNAME = os.getenv('TELEGRAM_CHANNEL_USERNAME', '@lpr1_treugolnik')
DATABASE_URL = os.getenv('DATABASE_URL', 'sqlite:///alerts.db')
GEO_REFERENCE_PATH = BASE_DIR / 'data' / 'geo_reference.csv'
CLASSIFIER_RULES_PATH = BASE_DIR / 'data' / 'classifier_rules.json'

def sqlite_path() -> Path:
    if not DATABASE_URL.startswith('sqlite:///'):
        raise ValueError('MVP supports only sqlite:/// DATABASE_URL')
    path = DATABASE_URL.replace('sqlite:///', '', 1)
    return Path(path) if Path(path).is_absolute() else BASE_DIR / path

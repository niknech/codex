from pathlib import Path
import sys
sys.path.append(str(Path(__file__).resolve().parents[1]))
from src.db import get_connection, init_db, utc_now
from src.parser import MessageParser

if __name__ == '__main__':
    init_db(); parser = MessageParser(); parsed = errors = 0
    with get_connection() as conn:
        rows = conn.execute('''SELECT r.* FROM raw_messages r
            LEFT JOIN parsed_alerts p ON p.raw_message_id=r.id AND p.parser_version=?
            WHERE p.id IS NULL''', (parser.parse(0,'','2000-01-01T00:00:00+00:00').parser_version,)).fetchall()
        for row in rows:
            try:
                p = parser.parse(row['id'], row['message_text'], row['message_date'])
                conn.execute('''INSERT OR IGNORE INTO parsed_alerts
                    (raw_message_id,event_date,event_time,region,city,settlement,alert_type,weapon_type,direction,status,confidence,parser_version,parse_notes)
                    VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)''', tuple(p.model_dump().values()))
                parsed += 1
            except Exception as e:
                conn.execute('INSERT INTO message_parse_errors (raw_message_id,error_type,error_text,created_at) VALUES (?,?,?,?)', (row['id'], type(e).__name__, str(e), utc_now()))
                errors += 1
        conn.commit()
    print(f'Парсинг завершен. Обработано: {parsed}. Ошибок: {errors}.')

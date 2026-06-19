from __future__ import annotations
import pandas as pd
from .db import get_connection

def load_dashboard_data() -> pd.DataFrame:
    q = '''SELECT r.id raw_message_id, r.message_date, r.message_text, r.message_url, r.views,
                  p.event_date, p.event_time, p.region, p.city, p.settlement, p.alert_type,
                  p.weapon_type, p.direction, p.status, p.confidence, p.parse_notes
           FROM raw_messages r LEFT JOIN parsed_alerts p ON p.raw_message_id = r.id'''
    with get_connection() as conn:
        return pd.read_sql_query(q, conn)

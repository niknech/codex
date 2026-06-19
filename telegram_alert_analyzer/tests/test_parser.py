from src.parser import MessageParser

def test_parser_unknown_message():
    p = MessageParser().parse(1, 'Обычное сообщение без тревог', '2026-01-01T12:30:00+00:00')
    assert p.alert_type == 'unknown'
    assert p.confidence <= 0.2
    assert p.event_date == '2026-01-01'

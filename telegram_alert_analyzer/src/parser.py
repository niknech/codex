from __future__ import annotations

from datetime import datetime
from .classifier import AlertClassifier
from .geo_resolver import GeoResolver
from .models import ParsedAlert

PARSER_VERSION = 'rule_based_v1'

class MessageParser:
    def __init__(self, geo_resolver: GeoResolver | None = None, classifier: AlertClassifier | None = None):
        self.geo = geo_resolver or GeoResolver()
        self.classifier = classifier or AlertClassifier()

    def parse(self, raw_message_id: int, message_text: str, message_date: str) -> ParsedAlert:
        dt = datetime.fromisoformat(message_date.replace('Z', '+00:00'))
        cls = self.classifier.classify(message_text)
        geo = self.geo.resolve(message_text)
        notes = '; '.join(x for x in [cls.notes, geo.notes] if x) or None
        confidence = cls.confidence_hint
        if cls.alert_type == 'unknown':
            confidence = min(confidence, 0.2)
        elif geo.ambiguous:
            confidence = min(confidence, 0.5)
        elif geo.city or geo.region:
            confidence = max(confidence, 0.9)
        else:
            confidence = min(confidence, 0.7)
        return ParsedAlert(
            raw_message_id=raw_message_id,
            event_date=dt.date().isoformat(), event_time=dt.time().replace(microsecond=0).isoformat(),
            region=geo.region, city=geo.city, alert_type=cls.alert_type, weapon_type=cls.weapon_type,
            direction=self.classifier.direction(message_text), status=cls.status,
            confidence=round(confidence, 2), parser_version=PARSER_VERSION, parse_notes=notes)

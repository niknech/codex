from __future__ import annotations

import json
from dataclasses import dataclass
from .config import CLASSIFIER_RULES_PATH

@dataclass
class Classification:
    alert_type: str
    weapon_type: str | None
    status: str
    confidence_hint: float
    notes: str | None = None

class AlertClassifier:
    def __init__(self, rules_path=CLASSIFIER_RULES_PATH):
        self.rules = json.loads(open(rules_path, encoding='utf-8').read())

    def classify(self, text: str) -> Classification:
        lower = text.lower()
        hits = {k for k in ('uav','missile','artillery','all_clear') if any(w in lower for w in self.rules[k])}
        is_update = any(w in lower for w in self.rules.get('update', []))
        threat_hits = hits - {'all_clear'}
        if 'all_clear' in hits:
            return Classification('all_clear', None, 'all_clear', 0.85)
        if len(threat_hits) > 1:
            return Classification('combined_alert', 'unknown', 'active', 0.8)
        if 'uav' in threat_hits:
            return Classification('uav_alert', 'uav', 'update' if is_update else 'active', 0.75)
        if 'missile' in threat_hits:
            weapon = 'ballistic_missile' if 'баллист' in lower else 'cruise_missile' if 'крылат' in lower else 'missile'
            return Classification('missile_alert', weapon, 'update' if is_update else 'active', 0.75)
        if 'artillery' in threat_hits:
            weapon = 'mlrs' if any(w in lower for w in ['рсзо','град']) else 'artillery'
            return Classification('artillery_alert', weapon, 'update' if is_update else 'active', 0.75)
        return Classification('unknown', 'unknown', 'unknown', 0.2, 'Тип угрозы не распознан')

    def direction(self, text: str) -> str | None:
        lower = text.lower()
        for d in self.rules.get('directions', []):
            if d in lower:
                return d
        return None

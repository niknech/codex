from __future__ import annotations

import csv
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Optional

from .config import GEO_REFERENCE_PATH

@dataclass
class GeoMatch:
    city: Optional[str]
    region: Optional[str]
    latitude: Optional[float] = None
    longitude: Optional[float] = None
    ambiguous: bool = False
    notes: Optional[str] = None

class GeoResolver:
    def __init__(self, path: Path = GEO_REFERENCE_PATH):
        self.entries = []
        with open(path, encoding='utf-8', newline='') as f:
            for row in csv.DictReader(f):
                aliases = [row['city'], row.get('region','')] + [a.strip() for a in row.get('aliases','').split(';') if a.strip()]
                self.entries.append({**row, 'aliases_list': list(dict.fromkeys([a for a in aliases if a]))})

    def resolve(self, text: str) -> GeoMatch:
        lower = text.lower()
        found = []
        for entry in self.entries:
            for alias in entry['aliases_list']:
                if alias and re.search(r'(?<![а-яёa-z])' + re.escape(alias.lower()) + r'(?![а-яёa-z])', lower):
                    found.append(entry); break
        unique = {(e['city'], e['region']): e for e in found}
        if len(unique) > 1:
            names = ', '.join(city for city, _ in unique)
            return GeoMatch(None, None, ambiguous=True, notes=f'Найдено несколько городов: {names}')
        if unique:
            e = next(iter(unique.values()))
            lat = float(e['latitude']) if e.get('latitude') else None
            lon = float(e['longitude']) if e.get('longitude') else None
            return GeoMatch(e['city'], e['region'], lat, lon)
        return GeoMatch(None, None)

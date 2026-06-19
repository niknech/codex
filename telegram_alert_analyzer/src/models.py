from __future__ import annotations

from dataclasses import dataclass, asdict
from typing import Optional, Literal
import importlib.util

AlertType = Literal['air_alert','missile_alert','uav_alert','artillery_alert','combined_alert','all_clear','unknown']
WeaponType = Literal['missile','ballistic_missile','cruise_missile','uav','artillery','mlrs','unknown']
Status = Literal['active','all_clear','update','unknown']

if importlib.util.find_spec('pydantic'):
    from pydantic import BaseModel, Field

    class ParsedAlert(BaseModel):
        raw_message_id: int
        event_date: str
        event_time: str
        region: Optional[str] = None
        city: Optional[str] = None
        settlement: Optional[str] = None
        alert_type: AlertType = 'unknown'
        weapon_type: Optional[WeaponType] = None
        direction: Optional[str] = None
        status: Status = 'unknown'
        confidence: float = Field(ge=0.0, le=1.0)
        parser_version: str
        parse_notes: Optional[str] = None
else:
    @dataclass
    class ParsedAlert:
        raw_message_id: int
        event_date: str
        event_time: str
        region: Optional[str] = None
        city: Optional[str] = None
        settlement: Optional[str] = None
        alert_type: AlertType = 'unknown'
        weapon_type: Optional[WeaponType] = None
        direction: Optional[str] = None
        status: Status = 'unknown'
        confidence: float = 0.0
        parser_version: str = 'unknown'
        parse_notes: Optional[str] = None

        def __post_init__(self) -> None:
            self.confidence = max(0.0, min(1.0, float(self.confidence)))

        def model_dump(self) -> dict:
            return asdict(self)

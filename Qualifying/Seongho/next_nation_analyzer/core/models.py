from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


Side = str


@dataclass
class MapData:
    n: int = 0
    k: int = 0
    x: list[int] = field(default_factory=list)
    y: list[int] = field(default_factory=list)
    strongholds: list[int] = field(default_factory=list)
    adjacency: list[list[int]] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "n": self.n,
            "k": self.k,
            "x": self.x,
            "y": self.y,
            "strongholds": self.strongholds,
            "adjacency": self.adjacency,
        }


@dataclass
class Command:
    turn: int
    side: Side
    type: str
    actor: str | None
    value: int | None
    raw: str

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__.copy()


@dataclass
class Event:
    turn: int
    type: str
    raw: str
    side: Side | None = None
    unit_id: str | None = None
    location: int | None = None
    amount: int | None = None
    cause: str | None = None
    time_used_ms: int | None = None
    time_tokens: int | None = None

    def to_dict(self) -> dict[str, Any]:
        return self.__dict__.copy()


@dataclass
class Turn:
    turn_no: int
    commands: dict[Side, list[Command]] = field(default_factory=lambda: {"A": [], "B": []})
    events: list[Event] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "turn_no": self.turn_no,
            "commands": {k: [c.to_dict() for c in v] for k, v in self.commands.items()},
            "events": [e.to_dict() for e in self.events],
        }


@dataclass
class Game:
    game_id: str
    source_file: str
    source_hash: str
    parser_version: str
    rule_version: str
    metadata: dict[str, Any] = field(default_factory=dict)
    map: MapData = field(default_factory=MapData)
    turns: list[Turn] = field(default_factory=list)
    result: dict[str, Any] = field(default_factory=dict)
    validation_status: str = "UNVALIDATED"
    warnings: list[dict[str, Any]] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {
            "game_id": self.game_id,
            "source_file": self.source_file,
            "source_hash": self.source_hash,
            "parser_version": self.parser_version,
            "rule_version": self.rule_version,
            "metadata": self.metadata,
            "map": self.map.to_dict(),
            "turns": [t.to_dict() for t in self.turns],
            "result": self.result,
            "validation_status": self.validation_status,
            "warnings": self.warnings,
        }


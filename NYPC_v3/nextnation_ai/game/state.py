"""Mutable game state data structures."""

from __future__ import annotations

import copy
from dataclasses import dataclass, field
from enum import Enum, IntEnum
from typing import Iterable

from .constants import (
    BASE_MAX_LEVEL,
    BASE_REPAIR_COST,
    BASE_TABLE,
    HQ_MAX_LEVEL,
    HQ_REPAIR_COST,
    HQ_TABLE,
    START_GOLD,
    START_TIME_TOKENS,
    START_WARRIORS,
)
from .map import GameMap


class Side(IntEnum):
    LEFT = 0
    RIGHT = 1

    @property
    def opponent(self) -> "Side":
        return Side.RIGHT if self is Side.LEFT else Side.LEFT

    @property
    def letter(self) -> str:
        return "A" if self is Side.LEFT else "B"

    @property
    def word(self) -> str:
        return "LEFT" if self is Side.LEFT else "RIGHT"

    @classmethod
    def from_letter(cls, letter: str) -> "Side":
        if letter == "A":
            return cls.LEFT
        if letter == "B":
            return cls.RIGHT
        raise ValueError(f"bad side letter: {letter}")

    @classmethod
    def from_word(cls, word: str) -> "Side":
        if word == "LEFT":
            return cls.LEFT
        if word == "RIGHT":
            return cls.RIGHT
        raise ValueError(f"bad side word: {word}")


class BuildingKind(str, Enum):
    HQ = "HQ"
    BASE = "BASE"


@dataclass(frozen=True, order=True)
class UnitId:
    owner: Side
    number: int

    def __str__(self) -> str:
        return f"{self.owner.letter}{self.number}"

    @classmethod
    def parse(cls, token: str) -> "UnitId":
        if len(token) < 2:
            raise ValueError(f"bad unit id: {token}")
        owner = Side.from_letter(token[0])
        number = int(token[1:])
        if number <= 0:
            raise ValueError(f"bad unit id number: {token}")
        return cls(owner, number)


@dataclass
class Unit:
    id: UnitId
    owner: Side
    pos: int
    hp: int
    max_hp: int
    target: int | None = None

    @property
    def alive(self) -> bool:
        return self.hp > 0

    @property
    def moving(self) -> bool:
        return self.target is not None


@dataclass
class Building:
    owner: Side
    kind: BuildingKind
    level: int
    hp: int

    @property
    def max_level(self) -> int:
        return HQ_MAX_LEVEL if self.kind is BuildingKind.HQ else BASE_MAX_LEVEL

    @property
    def max_hp(self) -> int:
        if self.kind is BuildingKind.HQ:
            return HQ_TABLE[self.level].max_hp
        return BASE_TABLE[self.level].max_hp

    @property
    def turret(self) -> int:
        if self.kind is BuildingKind.HQ:
            return HQ_TABLE[self.level].turret
        return BASE_TABLE[self.level].turret

    @property
    def worker_cap(self) -> int:
        if self.kind is BuildingKind.HQ:
            return HQ_TABLE[self.level].worker_cap
        return BASE_TABLE[self.level].worker_cap

    @property
    def train_cap(self) -> int:
        if self.kind is not BuildingKind.HQ:
            return 0
        return HQ_TABLE[self.level].train_cap

    @property
    def repair_cost(self) -> int:
        return HQ_REPAIR_COST if self.kind is BuildingKind.HQ else BASE_REPAIR_COST

    @property
    def next_level_cost(self) -> int | None:
        if self.level >= self.max_level:
            return None
        if self.kind is BuildingKind.HQ:
            return HQ_TABLE[self.level + 1].upgrade_cost
        return BASE_TABLE[self.level + 1].cost

    def upgrade_or_repair(self) -> None:
        if self.level >= self.max_level:
            self.hp = self.max_hp
            return
        self.level += 1
        self.hp = self.max_hp


@dataclass
class PlayerState:
    gold: int
    hq_pos: int
    next_unit_id: int = START_WARRIORS + 1
    remaining_time_tokens: int = START_TIME_TOKENS


@dataclass
class GameState:
    day: int
    map: GameMap
    players: dict[Side, PlayerState]
    units: dict[UnitId, Unit]
    buildings: dict[int, Building]
    done: bool = False
    winner: Side | None = None
    terminal_reason: str | None = None

    def clone(self) -> "GameState":
        return copy.deepcopy(self)

    def player(self, side: Side) -> PlayerState:
        return self.players[Side(side)]

    def get_hq(self, owner: Side) -> Building | None:
        b = self.buildings.get(self.player(owner).hq_pos)
        if b is None or b.owner is not owner or b.kind is not BuildingKind.HQ:
            return None
        return b

    def get_units_at(self, node: int, owner: Side | None = None, alive_only: bool = True) -> list[Unit]:
        out = []
        for unit in self.units.values():
            if unit.pos != node:
                continue
            if owner is not None and unit.owner is not owner:
                continue
            if alive_only and not unit.alive:
                continue
            out.append(unit)
        out.sort(key=lambda u: u.id.number)
        return out

    def get_idle_units_at(self, node: int, owner: Side, alive_only: bool = True) -> list[Unit]:
        return [
            unit
            for unit in self.get_units_at(node, owner, alive_only)
            if unit.target is None
        ]

    def units_for(self, owner: Side, alive_only: bool = True) -> list[Unit]:
        units = [
            unit
            for unit in self.units.values()
            if unit.owner is owner and (unit.alive or not alive_only)
        ]
        units.sort(key=lambda u: u.id.number)
        return units

    def buildings_for(self, owner: Side) -> list[tuple[int, Building]]:
        items = [(node, b) for node, b in self.buildings.items() if b.owner is owner]
        return sorted(items, key=lambda item: item[0])

    def has_alive_enemy_at(self, node: int, owner: Side) -> bool:
        return bool(self.get_units_at(node, owner.opponent, alive_only=True))

    def has_alive_friendly_at(self, node: int, owner: Side) -> bool:
        return bool(self.get_units_at(node, owner, alive_only=True))


def create_initial_state(game_map: GameMap) -> GameState:
    players = {
        Side.LEFT: PlayerState(START_GOLD, 0),
        Side.RIGHT: PlayerState(START_GOLD, game_map.N - 1),
    }
    buildings = {
        0: Building(Side.LEFT, BuildingKind.HQ, 1, HQ_TABLE[1].max_hp),
        game_map.N - 1: Building(Side.RIGHT, BuildingKind.HQ, 1, HQ_TABLE[1].max_hp),
    }
    units: dict[UnitId, Unit] = {}
    for side in (Side.LEFT, Side.RIGHT):
        hq = players[side].hq_pos
        for number in range(1, START_WARRIORS + 1):
            uid = UnitId(side, number)
            units[uid] = Unit(uid, side, hq, HQ_TABLE[1].warrior_hp, HQ_TABLE[1].warrior_hp)
    return GameState(0, game_map, players, units, buildings)

"""Game constants from the NEXT NATION rules."""

from __future__ import annotations

from dataclasses import dataclass

MAX_DAYS = 200
START_GOLD = 500
START_WARRIORS = 3
START_TIME_TOKENS = 5

TRAIN_COST = 120
MOVE_COST = 10
BASE_BUILD_COST = 300
INCOME_PER_WORKER = 15
FOOD_COST = 2

HQ_MAX_LEVEL = 5
BASE_MAX_LEVEL = 3
HQ_REPAIR_COST = 1000
BASE_REPAIR_COST = 500


@dataclass(frozen=True)
class HqLevel:
    upgrade_cost: int
    warrior_hp: int
    max_hp: int
    turret: int
    train_cap: int
    worker_cap: int


@dataclass(frozen=True)
class BaseLevel:
    cost: int
    max_hp: int
    turret: int
    worker_cap: int


HQ_TABLE: dict[int, HqLevel] = {
    1: HqLevel(0, 4, 10, 1, 1, 1),
    2: HqLevel(600, 5, 15, 2, 1, 2),
    3: HqLevel(1200, 6, 20, 2, 2, 3),
    4: HqLevel(2400, 7, 25, 3, 2, 4),
    5: HqLevel(3600, 8, 30, 3, 3, 5),
}

BASE_TABLE: dict[int, BaseLevel] = {
    1: BaseLevel(300, 6, 1, 1),
    2: BaseLevel(600, 12, 1, 2),
    3: BaseLevel(1000, 18, 2, 3),
}


def hq_level(level: int) -> HqLevel:
    return HQ_TABLE[level]


def base_level(level: int) -> BaseLevel:
    return BASE_TABLE[level]

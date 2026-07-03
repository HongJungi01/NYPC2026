#!/usr/bin/env python3
from __future__ import annotations

import math
import sys
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, NamedTuple

START_GOLD = 500
START_WARRIORS = 3
MOVE_COST = 10
TRAIN_COST = 120
WORK_INCOME = 15
UPKEEP_PER_WARRIOR = 2
HQ_MAX_LEVEL = 5
BASE_MAX_LEVEL = 3
HQ_HEAL_COST = 1000
BASE_HEAL_COST = 500


class HqLevelEntry(NamedTuple):
    upgrade_cost: int
    warrior_hp: int
    hp: int
    turret: int
    train_cap: int
    work_cap: int


class BaseLevelEntry(NamedTuple):
    cost: int
    hp: int
    turret: int
    work_cap: int


HQ_LEVELS: tuple[HqLevelEntry, ...] = (
    HqLevelEntry(0, 0, 0, 0, 0, 0),
    HqLevelEntry(0, 4, 10, 1, 1, 1),
    HqLevelEntry(600, 5, 15, 2, 1, 2),
    HqLevelEntry(1200, 6, 20, 2, 2, 3),
    HqLevelEntry(2400, 7, 25, 3, 2, 4),
    HqLevelEntry(3600, 8, 30, 3, 3, 5),
)
BASE_LEVELS: tuple[BaseLevelEntry, ...] = (
    BaseLevelEntry(0, 0, 0, 0),
    BaseLevelEntry(300, 6, 1, 1),
    BaseLevelEntry(600, 12, 1, 2),
    BaseLevelEntry(1000, 18, 2, 3),
)


class Side(Enum):
    LEFT = "A"
    RIGHT = "B"

    @property
    def opposite(self) -> "Side":
        return Side.RIGHT if self is Side.LEFT else Side.LEFT

    @classmethod
    def from_word(cls, w: str) -> "Side":
        return cls.LEFT if w == "LEFT" else cls.RIGHT

    @classmethod
    def from_char(cls, c: str) -> "Side":
        return cls.LEFT if c == "A" else cls.RIGHT


class BType(Enum):
    HQ = "HQ"
    BASE = "BASE"


class WState(Enum):
    STATIONARY = 0
    MOVING = 1


@dataclass(frozen=True)
class WarriorId:
    side: Side
    num: int

    def __str__(self) -> str:
        return f"{self.side.value}{self.num}"

    @classmethod
    def parse(cls, tok: str) -> "WarriorId":
        return cls(Side.from_char(tok[0]), int(tok[1:]))


@dataclass
class Warrior:
    id: WarriorId
    region: int
    hp: int
    state: WState = WState.STATIONARY
    target: int = 0


@dataclass
class Building:
    region: int
    side: Side
    type: BType
    level: int = 1
    hp: int = 10

    def max_level(self) -> int:
        return HQ_MAX_LEVEL if self.type is BType.HQ else BASE_MAX_LEVEL

    def current_hp(self) -> int:
        return HQ_LEVELS[self.level].hp if self.type is BType.HQ else BASE_LEVELS[self.level].hp

    def work_cap(self) -> int:
        return HQ_LEVELS[self.level].work_cap if self.type is BType.HQ else BASE_LEVELS[self.level].work_cap

    def train_cap(self) -> int:
        if self.type is not BType.HQ:
            return 0
        return HQ_LEVELS[self.level].train_cap

    def apply_upgrade(self) -> None:
        self.level += 1
        self.hp = self.current_hp()

    def upgrade_cost(self) -> int:
        if self.level >= self.max_level():
            return HQ_HEAL_COST if self.type is BType.HQ else BASE_HEAL_COST
        if self.type is BType.HQ:
            return HQ_LEVELS[self.level + 1].upgrade_cost
        return BASE_LEVELS[self.level + 1].cost


@dataclass
class GameMap:
    N: int = 0
    K: int = 0
    x: list[int] = field(default_factory=list)
    y: list[int] = field(default_factory=list)
    strongholds: list[int] = field(default_factory=list)
    adj: list[list[int]] = field(default_factory=list)
    my_side: Side = Side.LEFT
    my_hq: int = 0
    opp_hq: int = 0

    def hq_of(self, side: Side) -> int:
        return 0 if side is Side.LEFT else self.N - 1


@dataclass
class GameState:
    gold: int = START_GOLD
    my_countdown: int = 5
    opp_countdown: int = 5
    warriors: list[Warrior] = field(default_factory=list)
    buildings: list[Building] = field(default_factory=list)
    enemy_pressure_seen: bool = False
    enemy_wave_seen: bool = False
    enemy_early_train_seen: bool = False

    def find_building(self, region: int) -> Building | None:
        return next((b for b in self.buildings if b.region == region), None)

    def find_warrior(self, wid: WarriorId) -> Warrior | None:
        return next((w for w in self.warriors if w.id == wid), None)


@dataclass
class Actions:
    train_n: int = 0
    moves: list[tuple[WarriorId, int]] = field(default_factory=list)
    upgrades: list[int] = field(default_factory=list)


@dataclass
class Paths:
    dist: list[list[float]]
    nxt: list[list[int]]


DEFAULT_PROFILE: dict[str, Any] = {
    "name": "default",
    "initial_site_count": 2,
    "initial_keep_home": 1,
    "initial_raider_count": 0,
    "mark_opening_targets_claimed": False,
    "hold_new_bases": False,
    "hold_neutral_sites": False,
    "max_claimed_sites": 4,
    "site_home_weight": 1.0,
    "site_enemy_weight": 0.28,
    "site_center_weight": 0.10,
    "build_reserve": 90,
    "hq_upgrade_min_turn": 25,
    "hq_upgrade_after_bases": 1,
    "hq_upgrade_max_level": 3,
    "base_upgrade_min_turn": 55,
    "base_upgrade_reserve": 280,
    "desired_warriors": 15,
    "min_hq_level_before_training": 1,
    "train_growth_turns": 14,
    "train_reserve": 140,
    "attack_turn": 95,
    "enemy_swarm_attack_turn": None,
    "enemy_swarm_base_threshold": 999,
    "enemy_swarm_warrior_threshold": 999,
    "enemy_pressure_attack_turn": None,
    "enemy_pressure_field_threshold": 999,
    "enemy_pressure_hq_target_threshold": 999,
    "enemy_pressure_home_half_threshold": 999,
    "enemy_pressure_warrior_threshold": 999,
    "enemy_pressure_detect_until_turn": None,
    "enemy_pressure_rally_home": False,
    "enemy_pressure_attack_mode": None,
    "enemy_wave_launch_turn": None,
    "enemy_wave_field_threshold": 999,
    "enemy_wave_detect_min_turn": 0,
    "enemy_wave_detect_until_turn": 999,
    "enemy_early_train_threshold": 999,
    "enemy_early_train_detect_until_turn": 0,
    "enemy_early_train_attack_turn": None,
    "enemy_early_train_attack_mode": None,
    "enemy_early_train_rally_home": False,
    "attack_min_warriors": 11,
    "attack_target": "nearest_building",
    "min_hq_level_before_attack": 1,
    "attack_mode": "stream",
    "wave_launch_turn": 130,
    "wave_min_ready": 8,
    "attack_leave_workers": True,
    "attack_home_guard_count": 0,
    "attack_home_guard_if_enemy_wave": False,
    "attack_home_guard_disable_if_enemy_pressure": False,
    "attack_home_guard_disable_if_enemy_early_train": False,
    "worker_release_turn": None,
    "worker_release_enemy_worker_deficit": None,
    "rally_to_forward_base": True,
    "debug": False,
}


def debug(profile: dict[str, Any], msg: str) -> None:
    if profile.get("debug", False):
        print(msg, file=sys.stderr, flush=True)


def readln() -> str:
    line = sys.stdin.readline()
    if not line:
        sys.exit(0)
    return line.rstrip("\n")


def read_tokens() -> list[str]:
    return readln().split()


def make_base(region: int, side: Side) -> Building:
    return Building(region, side, BType.BASE, 1, BASE_LEVELS[1].hp)


def parse_init() -> tuple[GameMap, GameState]:
    game_map = GameMap()

    t = read_tokens()
    game_map.my_side = Side.from_word(t[1])

    t = read_tokens()
    game_map.N, game_map.K = int(t[0]), int(t[1])

    game_map.x = [int(v) for v in read_tokens()]
    game_map.y = [int(v) for v in read_tokens()]
    game_map.strongholds = sorted(int(v) for v in read_tokens())

    game_map.adj = [[] for _ in range(game_map.N)]
    for region in range(game_map.N):
        t = read_tokens()
        game_map.adj[region] = sorted(int(v) for v in t[1 : 1 + int(t[0])])

    game_map.my_hq = game_map.hq_of(game_map.my_side)
    game_map.opp_hq = game_map.hq_of(game_map.my_side.opposite)

    state = GameState()
    opp = game_map.my_side.opposite
    for suffix in range(1, START_WARRIORS + 1):
        state.warriors.append(
            Warrior(WarriorId(game_map.my_side, suffix), game_map.my_hq, HQ_LEVELS[1].warrior_hp)
        )
        state.warriors.append(
            Warrior(WarriorId(opp, suffix), game_map.opp_hq, HQ_LEVELS[1].warrior_hp)
        )
    state.buildings.append(Building(0, Side.LEFT, BType.HQ, 1, HQ_LEVELS[1].hp))
    state.buildings.append(Building(game_map.N - 1, Side.RIGHT, BType.HQ, 1, HQ_LEVELS[1].hp))

    print("OK", flush=True)
    return game_map, state


def read_turn_start() -> int | None:
    line = readln()
    if line == "FINISH":
        return None
    t = line.split()
    return int(t[2])


def read_turn_result(state: GameState, game_map: GameMap, submitted: Actions) -> None:
    for region in submitted.upgrades:
        b = state.find_building(region)
        if b is None:
            state.gold -= BASE_LEVELS[1].cost
            state.buildings.append(make_base(region, game_map.my_side))
        else:
            if b.level >= b.max_level():
                state.gold -= b.upgrade_cost()
                b.hp = b.current_hp()
            else:
                state.gold -= b.upgrade_cost()
                b.apply_upgrade()

    for wid, target in submitted.moves:
        b = state.find_building(target)
        state.gold -= 0 if (b is not None and b.side is game_map.my_side) else MOVE_COST
        w = state.find_warrior(wid)
        if w is not None:
            w.state = WState.MOVING
            w.target = target

    state.gold -= TRAIN_COST * submitted.train_n

    line = readln()
    if line == "FINISH":
        sys.exit(0)
    t = line.split()
    if not t or t[0] != "TURN":
        sys.exit(0)

    t = read_tokens()
    state.my_countdown = int(t[2])
    state.opp_countdown = int(t[4])

    t = read_tokens()
    upgrade_count = int(t[1])
    for _ in range(upgrade_count):
        r = read_tokens()
        side = Side.from_char(r[0][0])
        region = int(r[1])
        b = state.find_building(region)
        if b is None:
            state.buildings.append(make_base(region, side))
        elif b.side is not game_map.my_side:
            if b.level >= b.max_level():
                b.hp = b.current_hp()
            else:
                b.apply_upgrade()

    t = read_tokens()
    train_count = int(t[1])
    if train_count > 0:
        ids = read_tokens()
        for i in range(train_count):
            wid = WarriorId.parse(ids[i])
            hq_region = game_map.hq_of(wid.side)
            hq_b = state.find_building(hq_region)
            hq_level = hq_b.level if hq_b is not None else 1
            state.warriors.append(Warrior(wid, hq_region, HQ_LEVELS[hq_level].warrior_hp))

    t = read_tokens()
    move_count = int(t[1])
    for _ in range(move_count):
        r = read_tokens()
        wid = WarriorId.parse(r[0])
        region = int(r[1])
        w = state.find_warrior(wid)
        if w is not None:
            w.region = region
            if wid.side is game_map.my_side and w.state is WState.MOVING and w.region == w.target:
                w.state = WState.STATIONARY

    t = read_tokens()
    damage_count = int(t[1])
    for _ in range(damage_count):
        r = read_tokens()
        wid = WarriorId.parse(r[1])
        damage = int(r[2])
        w = state.find_warrior(wid)
        if w is not None:
            w.hp -= damage
    state.warriors = [w for w in state.warriors if w.hp > 0]

    t = read_tokens()
    siege_count = int(t[1])
    for _ in range(siege_count):
        r = read_tokens()
        region = int(r[1])
        damage = int(r[2])
        b = state.find_building(region)
        if b is not None:
            b.hp -= damage
    state.buildings = [b for b in state.buildings if b.hp > 0]

    readln()

    income = 0
    for b in state.buildings:
        if b.side is not game_map.my_side:
            continue
        count = sum(1 for w in state.warriors if w.id.side is game_map.my_side and w.region == b.region)
        income += WORK_INCOME * min(count, b.work_cap())
    state.gold += income

    alive = sum(1 for w in state.warriors if w.id.side is game_map.my_side)
    state.gold = max(0, state.gold - UPKEEP_PER_WARRIOR * alive)


def euclid_ceil(game_map: GameMap, u: int, v: int) -> float:
    return math.ceil(math.hypot(game_map.x[u] - game_map.x[v], game_map.y[u] - game_map.y[v]))


def calculate_paths(game_map: GameMap) -> Paths:
    inf = math.inf
    n = game_map.N
    dist = [[inf] * n for _ in range(n)]
    nxt = [[-1] * n for _ in range(n)]

    for i in range(n):
        dist[i][i] = 0.0
        nxt[i][i] = i
    for u in range(n):
        for v in game_map.adj[u]:
            dist[u][v] = min(dist[u][v], euclid_ceil(game_map, u, v))

    for k in range(n):
        dk = dist[k]
        for u in range(n):
            duk = dist[u][k]
            if duk == inf:
                continue
            du = dist[u]
            for v in range(n):
                cand = duk + dk[v]
                if cand < du[v]:
                    du[v] = cand

    for u in range(n):
        for v in range(n):
            if u == v or dist[u][v] == inf:
                continue
            best_score = inf
            best_neighbor = -1
            for nb in game_map.adj[u]:
                if dist[nb][v] == inf:
                    continue
                score = euclid_ceil(game_map, u, nb) + dist[nb][v]
                if score < best_score or (score == best_score and nb < best_neighbor):
                    best_score = score
                    best_neighbor = nb
            nxt[u][v] = best_neighbor
    return Paths(dist, nxt)


def merge_profile(profile: dict[str, Any] | None) -> dict[str, Any]:
    merged = dict(DEFAULT_PROFILE)
    if profile:
        merged.update(profile)
    return merged


def friendly_buildings(state: GameState, side: Side) -> list[Building]:
    return [b for b in state.buildings if b.side is side]


def enemy_present(state: GameState, side: Side, region: int) -> bool:
    return any(w.hp > 0 and w.id.side is side.opposite and w.region == region for w in state.warriors)


def friendly_present(state: GameState, side: Side, region: int) -> bool:
    return any(w.hp > 0 and w.id.side is side and w.region == region for w in state.warriors)


def warrior_count(state: GameState, side: Side, region: int) -> int:
    return sum(1 for w in state.warriors if w.hp > 0 and w.id.side is side and w.region == region)


def alive_warriors(state: GameState, side: Side) -> list[Warrior]:
    return [w for w in state.warriors if w.hp > 0 and w.id.side is side]


def stationary_warriors(state: GameState, side: Side) -> list[Warrior]:
    return [w for w in alive_warriors(state, side) if w.state is WState.STATIONARY]


def moving_targets(state: GameState, side: Side) -> set[int]:
    return {w.target for w in alive_warriors(state, side) if w.state is WState.MOVING}


def site_score(profile: dict[str, Any], game_map: GameMap, paths: Paths, site: int) -> float:
    home = paths.dist[game_map.my_hq][site]
    enemy = paths.dist[site][game_map.opp_hq]
    center = paths.dist[site][game_map.N // 2]
    return (
        profile["site_home_weight"] * home
        + profile["site_enemy_weight"] * enemy
        + profile["site_center_weight"] * center
    )


def sorted_sites(profile: dict[str, Any], game_map: GameMap, paths: Paths) -> list[int]:
    return sorted(game_map.strongholds, key=lambda r: (site_score(profile, game_map, paths, r), r))


def can_upgrade_here(state: GameState, game_map: GameMap, side: Side, region: int) -> bool:
    if region not in set(game_map.strongholds) | {0, game_map.N - 1}:
        return False
    if enemy_present(state, side, region):
        return False
    if not friendly_present(state, side, region):
        return False
    b = state.find_building(region)
    return b is None or b.side is side


def planned_move_cost(state: GameState, side: Side, target: int, planned_friendly: set[int]) -> int:
    b = state.find_building(target)
    if target in planned_friendly or (b is not None and b.side is side):
        return 0
    return MOVE_COST


def emit(actions: Actions) -> None:
    out = ["COMMAND"]
    for wid, target in actions.moves:
        out.append(f"MOVE {wid} {target}")
    for region in actions.upgrades:
        out.append(f"UPGRADE {region}")
    if actions.train_n > 0:
        out.append(f"TRAIN {actions.train_n}")
    out.append("END")
    sys.stdout.write("\n".join(out) + "\n")
    sys.stdout.flush()


def decide(state: GameState, game_map: GameMap, paths: Paths, turn: int, profile: dict[str, Any]) -> Actions:
    side = game_map.my_side
    actions = Actions()
    budget = state.gold
    upgrade_regions: set[int] = set()
    planned_friendly = {b.region for b in friendly_buildings(state, side)}
    new_base_regions: set[int] = set()
    site_set = set(game_map.strongholds)
    ordered_sites = sorted_sites(profile, game_map, paths)
    my_warriors = alive_warriors(state, side)
    owned_site_count = sum(1 for b in friendly_buildings(state, side) if b.region in site_set)

    def add_upgrade(region: int, cost: int) -> bool:
        nonlocal budget
        if region in upgrade_regions or cost > budget:
            return False
        if not can_upgrade_here(state, game_map, side, region):
            return False
        was_empty = state.find_building(region) is None
        actions.upgrades.append(region)
        upgrade_regions.add(region)
        planned_friendly.add(region)
        if was_empty and region in site_set:
            new_base_regions.add(region)
        budget -= cost
        return True

    # First convert occupied neutral sites into income. This is the highest ROI action.
    build_candidates = []
    for region in ordered_sites:
        if state.find_building(region) is None and friendly_present(state, side, region):
            build_candidates.append(region)
    for region in build_candidates:
        reserve = int(profile["build_reserve"])
        if budget >= BASE_LEVELS[1].cost + reserve:
            add_upgrade(region, BASE_LEVELS[1].cost)

    # Upgrade HQ when the economy can survive the expense.
    hq = state.find_building(game_map.my_hq)
    if (
        hq is not None
        and hq.side is side
        and hq.level < min(HQ_MAX_LEVEL, int(profile["hq_upgrade_max_level"]))
        and turn >= int(profile["hq_upgrade_min_turn"])
        and owned_site_count >= int(profile["hq_upgrade_after_bases"])
    ):
        cost = hq.upgrade_cost()
        if budget >= cost + int(profile["train_reserve"]):
            add_upgrade(game_map.my_hq, cost)

    # Mature staffed bases to level 2/3 after the first expansion wave.
    if turn >= int(profile["base_upgrade_min_turn"]):
        bases = [
            b
            for b in friendly_buildings(state, side)
            if b.type is BType.BASE and b.level < BASE_MAX_LEVEL and warrior_count(state, side, b.region) >= b.work_cap()
        ]
        bases.sort(key=lambda b: (site_score(profile, game_map, paths, b.region), b.region))
        for b in bases:
            cost = b.upgrade_cost()
            if budget >= cost + int(profile["base_upgrade_reserve"]):
                add_upgrade(b.region, cost)
                break

    hq_after_level = hq.level if hq is not None else 1
    if game_map.my_hq in upgrade_regions and hq is not None and hq.level < HQ_MAX_LEVEL:
        hq_after_level += 1

    targeted_sites = moving_targets(state, side)
    occupied_neutral_sites = {
        r for r in site_set if state.find_building(r) is None and friendly_present(state, side, r)
    }
    claimed_sites = planned_friendly | targeted_sites
    if profile.get("hold_neutral_sites", False):
        claimed_sites |= occupied_neutral_sites
    move_used: set[WarriorId] = set()
    opening_targets = [r for r in ordered_sites if r not in claimed_sites]

    def add_move(warrior: Warrior, target: int) -> bool:
        nonlocal budget
        if warrior.id in move_used or warrior.state is not WState.STATIONARY or target == warrior.region:
            return False
        if target < 0 or target >= game_map.N or paths.nxt[warrior.region][target] == -1:
            return False
        cost = planned_move_cost(state, side, target, planned_friendly)
        if cost > budget:
            return False
        actions.moves.append((warrior.id, target))
        move_used.add(warrior.id)
        budget -= cost
        return True

    # Opening: send initial units to the best build sites, but keep a home worker.
    if turn == 1:
        initial_slots = max(0, int(profile["initial_site_count"]))
        keep_home = max(0, int(profile["initial_keep_home"]))
        candidates = [w for w in stationary_warriors(state, side) if w.region == game_map.my_hq]
        candidates.sort(key=lambda w: w.id.num)
        movable = candidates[keep_home:]
        raider_count = max(0, int(profile.get("initial_raider_count", 0)))
        for warrior in movable[:raider_count]:
            add_move(warrior, game_map.opp_hq)
        site_movers = [w for w in movable[raider_count:] if w.id not in move_used]
        for warrior, target in zip(site_movers[:initial_slots], opening_targets):
            if add_move(warrior, target) and profile.get("mark_opening_targets_claimed", False):
                claimed_sites.add(target)

    # Keep enough workers at each building before assigning explorers/attackers.
    building_need: dict[int, int] = {}
    for b in friendly_buildings(state, side):
        need = b.work_cap()
        if b.region in upgrade_regions and b.type is BType.BASE and b.level < BASE_MAX_LEVEL:
            need = BASE_LEVELS[b.level + 1].work_cap
        if b.region == game_map.my_hq:
            need = max(1, min(need, hq_after_level + 1))
        building_need[b.region] = need
    if profile.get("hold_new_bases", False):
        for region in new_base_regions:
            building_need[region] = max(building_need.get(region, 0), BASE_LEVELS[1].work_cap)

    reserved_ids: set[WarriorId] = set()
    for region, need in building_need.items():
        local = [w for w in my_warriors if w.region == region]
        local.sort(key=lambda w: (w.state.value, w.id.num))
        for w in local[:need]:
            reserved_ids.add(w.id)
    if profile.get("hold_neutral_sites", False):
        for region in occupied_neutral_sites:
            local = [w for w in my_warriors if w.region == region]
            local.sort(key=lambda w: (w.state.value, w.id.num))
            if local:
                reserved_ids.add(local[0].id)

    # Send idle units to understaffed friendly buildings. Destination is free.
    underfilled = []
    for region, need in building_need.items():
        have = warrior_count(state, side, region)
        if have < need:
            b = state.find_building(region)
            priority = site_score(profile, game_map, paths, region) if region in site_set else 0
            underfilled.append((priority, region, need - have, b is not None and b.type is BType.HQ))
    underfilled.sort(key=lambda row: (row[3], row[0], row[1]))

    for _, region, need, _is_hq in underfilled:
        for warrior in sorted(stationary_warriors(state, side), key=lambda w: (paths.dist[w.region][region], w.id.num)):
            if need <= 0:
                break
            if warrior.id in reserved_ids or warrior.id in move_used or enemy_present(state, side, warrior.region):
                continue
            if add_move(warrior, region):
                reserved_ids.add(warrior.id)
                need -= 1

    # Expand until the profile's site cap is reached.
    target_cap = int(profile["max_claimed_sites"])
    claimed_count = sum(1 for r in game_map.strongholds if r in claimed_sites)
    if claimed_count < target_cap:
        targets = [r for r in ordered_sites if r not in claimed_sites and not enemy_present(state, side, r)]
        for target in targets:
            if claimed_count >= target_cap:
                break
            candidates = [
                w
                for w in stationary_warriors(state, side)
                if w.id not in reserved_ids and w.id not in move_used and not enemy_present(state, side, w.region)
            ]
            if not candidates:
                break
            candidates.sort(key=lambda w: (paths.dist[w.region][target], w.id.num))
            if add_move(candidates[0], target):
                claimed_sites.add(target)
                claimed_count += 1

    # Massed pressure after economy is online or the game is old enough.
    warrior_total = len(my_warriors)
    enemy_site_count = sum(1 for b in friendly_buildings(state, side.opposite) if b.region in site_set)
    enemy_warrior_total = len(alive_warriors(state, side.opposite))
    effective_attack_turn = int(profile["attack_turn"])
    swarm_attack_turn = profile.get("enemy_swarm_attack_turn")
    if swarm_attack_turn is not None and (
        enemy_site_count >= int(profile.get("enemy_swarm_base_threshold", 999))
        or enemy_warrior_total >= int(profile.get("enemy_swarm_warrior_threshold", 999))
    ):
        effective_attack_turn = int(swarm_attack_turn)
    enemy_building_regions = {b.region for b in friendly_buildings(state, side.opposite)}
    enemy_field_count = sum(
        1 for w in alive_warriors(state, side.opposite) if w.region not in enemy_building_regions
    )
    early_train_detect_until = int(profile.get("enemy_early_train_detect_until_turn", 0))
    if (
        turn < early_train_detect_until
        and enemy_warrior_total >= int(profile.get("enemy_early_train_threshold", 999))
    ):
        state.enemy_early_train_seen = True

    wave_launch_turn = profile.get("enemy_wave_launch_turn")
    if wave_launch_turn is not None:
        wave_detect_min = int(profile.get("enemy_wave_detect_min_turn", 0))
        wave_detect_until = int(profile.get("enemy_wave_detect_until_turn", 999))
        if (
            wave_detect_min <= turn < wave_detect_until
            and enemy_field_count >= int(profile.get("enemy_wave_field_threshold", 999))
        ):
            state.enemy_wave_seen = True

    pressure_attack_turn = profile.get("enemy_pressure_attack_turn")
    if pressure_attack_turn is not None:
        pressure_detect_until = profile.get("enemy_pressure_detect_until_turn")
        if pressure_detect_until is None:
            pressure_detect_until = effective_attack_turn
        enemy_hq_target_count = sum(
            1
            for w in alive_warriors(state, side.opposite)
            if w.state is WState.MOVING and w.target == game_map.my_hq
        )
        enemy_home_half_count = sum(
            1
            for w in alive_warriors(state, side.opposite)
            if w.region not in enemy_building_regions
            and paths.dist[w.region][game_map.my_hq] <= paths.dist[w.region][game_map.opp_hq]
        )
        if turn < int(pressure_detect_until) and (
            enemy_field_count >= int(profile.get("enemy_pressure_field_threshold", 999))
            or enemy_hq_target_count >= int(profile.get("enemy_pressure_hq_target_threshold", 999))
            or enemy_home_half_count >= int(profile.get("enemy_pressure_home_half_threshold", 999))
            or enemy_warrior_total >= int(profile.get("enemy_pressure_warrior_threshold", 999))
        ):
            state.enemy_pressure_seen = True
        if state.enemy_pressure_seen:
            effective_attack_turn = int(pressure_attack_turn)
    early_train_attack_turn = profile.get("enemy_early_train_attack_turn")
    if state.enemy_early_train_seen and early_train_attack_turn is not None:
        effective_attack_turn = int(early_train_attack_turn)
    should_attack = (turn >= effective_attack_turn or warrior_total >= int(profile["attack_min_warriors"])) and hq_after_level >= int(profile.get("min_hq_level_before_attack", 1))
    bases = [b for b in friendly_buildings(state, side) if b.type is BType.BASE]
    forward_base = None
    if bases:
        forward_base = min(bases, key=lambda b: (paths.dist[b.region][game_map.opp_hq], b.region)).region

    release_turn = profile.get("worker_release_turn")
    preserve_workers = bool(profile["attack_leave_workers"])
    if release_turn is not None and turn >= int(release_turn):
        release_allowed = True
        enemy_deficit_threshold = profile.get("worker_release_enemy_worker_deficit")
        if enemy_deficit_threshold is not None:
            enemy_worker_deficit = sum(
                max(0, b.work_cap() - warrior_count(state, side.opposite, b.region))
                for b in friendly_buildings(state, side.opposite)
            )
            release_allowed = enemy_worker_deficit >= int(enemy_deficit_threshold)
        if release_allowed:
            preserve_workers = False

    home_guard_ids: set[WarriorId] = set()
    home_guard_count = int(profile.get("attack_home_guard_count", 0))
    guard_needs_wave = bool(profile.get("attack_home_guard_if_enemy_wave", False))
    guard_disabled = (
        bool(profile.get("attack_home_guard_disable_if_enemy_pressure", False)) and state.enemy_pressure_seen
    ) or (
        bool(profile.get("attack_home_guard_disable_if_enemy_early_train", False)) and state.enemy_early_train_seen
    )
    if home_guard_count > 0 and not guard_disabled and (not guard_needs_wave or state.enemy_wave_seen):
        candidates = [
            w
            for w in stationary_warriors(state, side)
            if w.id not in move_used
            and w.id not in reserved_ids
            and not enemy_present(state, side, w.region)
        ]
        candidates.sort(key=lambda w: (paths.dist[w.region][game_map.my_hq], w.id.num))
        home_guard_ids = {w.id for w in candidates[:home_guard_count]}

    def surplus_stationary() -> list[Warrior]:
        return [
            w
            for w in stationary_warriors(state, side)
            if w.id not in move_used
            and not enemy_present(state, side, w.region)
            and not (preserve_workers and w.id in reserved_ids)
            and w.id not in home_guard_ids
        ]

    active_attack_mode = profile.get("attack_mode")
    pressure_attack_mode = profile.get("enemy_pressure_attack_mode")
    early_train_attack_mode = profile.get("enemy_early_train_attack_mode")
    if state.enemy_pressure_seen and pressure_attack_mode is not None:
        active_attack_mode = pressure_attack_mode
    if state.enemy_early_train_seen and early_train_attack_mode is not None:
        active_attack_mode = early_train_attack_mode

    should_rally_home = (
        state.enemy_pressure_seen and profile.get("enemy_pressure_rally_home", False)
    ) or (
        state.enemy_early_train_seen and profile.get("enemy_early_train_rally_home", False)
    )
    if should_rally_home and not should_attack:
        for warrior in sorted(surplus_stationary(), key=lambda w: (paths.dist[w.region][game_map.my_hq], w.id.num)):
            if warrior.region == game_map.my_hq:
                continue
            add_move(warrior, game_map.my_hq)
    elif should_attack and active_attack_mode == "wave" and forward_base is not None:
        ready = sum(1 for w in alive_warriors(state, side) if w.region == forward_base and w.id not in reserved_ids)
        launch_turn = int(profile.get("wave_launch_turn", 130))
        if state.enemy_wave_seen and wave_launch_turn is not None:
            launch_turn = int(wave_launch_turn)
        launch = turn >= launch_turn or ready >= int(profile.get("wave_min_ready", 8))
        if launch:
            for warrior in sorted(surplus_stationary(), key=lambda w: (paths.dist[w.region][game_map.opp_hq], w.id.num)):
                add_move(warrior, game_map.opp_hq)
        else:
            for warrior in sorted(surplus_stationary(), key=lambda w: (paths.dist[w.region][forward_base], w.id.num)):
                if warrior.region == forward_base:
                    continue
                if add_move(warrior, forward_base):
                    break
    elif should_attack:
        enemy_buildings = [b.region for b in state.buildings if b.side is side.opposite]
        for warrior in sorted(surplus_stationary(), key=lambda w: (w.region, w.id.num)):
            target = game_map.opp_hq
            if enemy_buildings and profile.get("attack_target") != "hq":
                target = min(enemy_buildings, key=lambda r: (paths.dist[warrior.region][r], r))
            add_move(warrior, target)
    elif profile["rally_to_forward_base"] and forward_base is not None:
        for warrior in sorted(stationary_warriors(state, side), key=lambda w: (paths.dist[w.region][forward_base], w.id.num)):
            if warrior.id in move_used or warrior.id in reserved_ids or warrior.region == forward_base:
                continue
            if add_move(warrior, forward_base):
                break

    # Train last with whatever budget remains.
    hq_exists = hq is not None and hq.side is side
    if hq_exists:
        train_cap = HQ_LEVELS[hq_after_level].train_cap
        desired = min(int(profile["desired_warriors"]), START_WARRIORS + turn // int(profile["train_growth_turns"]) + owned_site_count * 2)
        can_train_now = hq_after_level >= int(profile.get("min_hq_level_before_training", 1))
        if can_train_now and warrior_total < desired and budget >= TRAIN_COST + int(profile["train_reserve"]):
            affordable = max(0, (budget - int(profile["train_reserve"])) // TRAIN_COST)
            actions.train_n = max(0, min(train_cap, affordable, desired - warrior_total))

    debug(profile, f"turn={turn} gold={state.gold} budget={budget} actions={actions}")
    return actions


def run_bot(profile: dict[str, Any] | None = None) -> None:
    merged = merge_profile(profile)
    game_map, state = parse_init()
    paths = calculate_paths(game_map)
    while (turn := read_turn_start()) is not None:
        actions = decide(state, game_map, paths, turn, merged)
        emit(actions)
        read_turn_result(state, game_map, actions)



PROFILE: dict[str, Any] = {
    "name": "v31_k_window_reserve",
    "initial_site_count": 2,
    "initial_keep_home": 1,
    "initial_raider_count": 0,
    "mark_opening_targets_claimed": True,
    "hold_new_bases": True,
    "hold_neutral_sites": True,
    "max_claimed_sites": 7,
    "site_home_weight": 1.35,
    "site_enemy_weight": 0.00,
    "site_center_weight": 0.02,
    "build_reserve": 0,
    "hq_upgrade_min_turn": 18,
    "hq_upgrade_after_bases": 1,
    "hq_upgrade_max_level": 5,
    "base_upgrade_min_turn": 65,
    "base_upgrade_reserve": 620,
    "desired_warriors": 90,
    "min_hq_level_before_training": 2,
    "train_growth_turns": 3,
    "train_reserve": 180,
    "attack_turn": 140,
    "enemy_swarm_attack_turn": 140,
    "enemy_swarm_base_threshold": 4,
    "enemy_swarm_warrior_threshold": 32,
    "enemy_pressure_attack_turn": 169,
    "enemy_pressure_field_threshold": 20,
    "enemy_pressure_detect_until_turn": 160,
    "enemy_pressure_rally_home": True,
    "enemy_wave_launch_turn": 159,
    "enemy_wave_field_threshold": 2,
    "enemy_wave_detect_min_turn": 140,
    "enemy_wave_detect_until_turn": 159,
    "enemy_early_train_threshold": 4,
    "enemy_early_train_detect_until_turn": 14,
    "enemy_early_train_attack_turn": 120,
    "enemy_early_train_attack_mode": "stream",
    "enemy_early_train_rally_home": True,
    "attack_min_warriors": 70,
    "min_hq_level_before_attack": 2,
    "attack_target": "hq",
    "attack_mode": "wave",
    "wave_launch_turn": 162,
    "wave_min_ready": 999,
    "attack_leave_workers": True,
    "worker_release_turn": 190,
    "attack_home_guard_count": 8,
    "attack_home_guard_if_enemy_wave": False,
    "attack_home_guard_disable_if_enemy_early_train": True,
    "rally_to_forward_base": True,
}


def profile_for_map(k: int) -> dict[str, object]:
    profile = dict(PROFILE)
    if 11 <= k <= 15:
        profile["train_reserve"] = 240
    return profile


if __name__ == "__main__":
    game_map, state = parse_init()
    merged = merge_profile(profile_for_map(game_map.K))
    paths = calculate_paths(game_map)
    while (turn := read_turn_start()) is not None:
        actions = decide(state, game_map, paths, turn, merged)
        emit(actions)
        read_turn_result(state, game_map, actions)

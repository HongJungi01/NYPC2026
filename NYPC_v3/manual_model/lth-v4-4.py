#!/usr/bin/env python3
"""
NEXT NATION bot — v5
"""
from __future__ import annotations

import math
import os
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

# ───────────────────────────── 0. Rule constants ──────────────────────────────
MAX_TURN = 200
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
BASE_BUILD_COST = 300

# Level tables, indexed by level (0 == "no building").
# HQ:   (upgrade_cost, warrior_hp, hp, turret, train_cap, work_cap)
HQ_LEVELS = (
    (0,    0, 0,  0, 0, 0),
    (0,    4, 10, 1, 1, 1),
    (600,  5, 15, 2, 1, 2),
    (1200, 6, 20, 2, 2, 3),
    (2400, 7, 25, 3, 2, 4),
    (3600, 8, 30, 3, 3, 5),
)
# BASE: (cost, hp, turret, work_cap)
BASE_LEVELS = (
    (0,    0,  0, 0),
    (300,  6,  1, 1),
    (600,  12, 1, 2),
    (1000, 18, 2, 3),
)

DEBUG = bool(os.environ.get("BOT_DEBUG"))


def dbg(*args) -> None:
    if DEBUG:
        sys.stderr.write(" ".join(str(a) for a in args) + "\n")
        sys.stderr.flush()


# ───────────────────────────── 1. Data model ──────────────────────────────────
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


@dataclass
class Warrior:
    wid: str               # "A12" / "B7"
    side: Side
    suffix: int
    region: int
    hp: int
    moving: bool = False           # known exactly for me; inferred for enemy
    target: Optional[int] = None   # my move target (known)
    prev_region: Optional[int] = None


@dataclass
class Building:
    region: int
    side: Side
    type: BType
    level: int
    hp: int

    @property
    def max_level(self) -> int:
        return HQ_MAX_LEVEL if self.type is BType.HQ else BASE_MAX_LEVEL

    @property
    def max_hp(self) -> int:
        if self.type is BType.HQ:
            return HQ_LEVELS[self.level][2]
        return BASE_LEVELS[self.level][1]

    @property
    def turret(self) -> int:
        if self.type is BType.HQ:
            return HQ_LEVELS[self.level][3]
        return BASE_LEVELS[self.level][2]

    @property
    def work_cap(self) -> int:
        if self.type is BType.HQ:
            return HQ_LEVELS[self.level][5]
        return BASE_LEVELS[self.level][3]

    @property
    def train_cap(self) -> int:
        return HQ_LEVELS[self.level][4] if self.type is BType.HQ else 0

    @property
    def heal_cost(self) -> int:
        return HQ_HEAL_COST if self.type is BType.HQ else BASE_HEAL_COST

    def next_cost(self) -> int:
        """Cost of the next construction action (upgrade if possible, else heal)."""
        if self.level >= self.max_level:
            return self.heal_cost
        if self.type is BType.HQ:
            return HQ_LEVELS[self.level + 1][0]
        return BASE_LEVELS[self.level + 1][0]

    def apply_upgrade_or_heal(self) -> None:
        if self.level < self.max_level:
            self.level += 1
        self.hp = self.max_hp


@dataclass
class GameMap:
    N: int = 0
    K: int = 0
    x: list[int] = field(default_factory=list)
    y: list[int] = field(default_factory=list)
    sites: list[int] = field(default_factory=list)
    site_set: set[int] = field(default_factory=set)
    adj: list[list[int]] = field(default_factory=list)
    me: Side = Side.LEFT
    my_hq: int = 0
    enemy_hq: int = 0


@dataclass
class GameState:
    gold: int = START_GOLD
    enemy_gold_est: int = START_GOLD
    my_countdown: int = 5
    enemy_countdown: int = 5
    warriors: dict[str, Warrior] = field(default_factory=dict)
    buildings: dict[int, Building] = field(default_factory=dict)

    def building_at(self, region: int) -> Optional[Building]:
        return self.buildings.get(region)


@dataclass
class Actions:
    train_n: int = 0
    moves: list[tuple[str, int]] = field(default_factory=list)
    upgrades: list[int] = field(default_factory=list)


# ───────────────────────────── 2. Paths ───────────────────────────────────────
class Paths:
    """All-pairs distances (sum of ceil-Euclidean edge weights) plus the
    deterministic next-hop and day-distance used by the movement rule."""

    def __init__(self, m: GameMap):
        self.m = m
        self.dist = self._all_pairs(m)
        self._next: dict[tuple[int, int], int] = {}
        self._days: dict[tuple[int, int], int] = {}

    @staticmethod
    def _w(m: GameMap, u: int, v: int) -> int:
        return math.ceil(math.hypot(m.x[u] - m.x[v], m.y[u] - m.y[v]))

    def _all_pairs(self, m: GameMap) -> list[list[float]]:
        import heapq
        INF = math.inf
        N = m.N
        dist = [[INF] * N for _ in range(N)]
        for s in range(N):
            d = dist[s]
            d[s] = 0
            heap = [(0, s)]
            while heap:
                du, u = heapq.heappop(heap)
                if du != d[u]:
                    continue
                for v in m.adj[u]:
                    nd = du + self._w(m, u, v)
                    if nd < d[v]:
                        d[v] = nd
                        heapq.heappush(heap, (nd, v))
        return dist

    def reachable(self, u: int, v: int) -> bool:
        return self.dist[u][v] != math.inf

    def next_step(self, u: int, v: int) -> int:
        """Next zone a warrior at u heading to v moves to (greedy min
        edge_weight + dist-to-target, ties -> smaller zone number)."""
        if u == v:
            return v
        key = (u, v)
        c = self._next.get(key)
        if c is not None:
            return c
        best_v, best_score = -1, math.inf
        for nb in sorted(self.m.adj[u]):           # sorted -> tie to smaller id
            dv = self.dist[nb][v]
            if dv == math.inf:
                continue
            score = self._w(self.m, u, nb) + dv
            if score < best_score:
                best_score = score
                best_v = nb
        self._next[key] = best_v
        return best_v

    def days(self, u: int, v: int) -> int:
        """Number of days (one zone/day) for a warrior at u to reach v."""
        if u == v:
            return 0
        key = (u, v)
        c = self._days.get(key)
        if c is not None:
            return c
        cur, steps = u, 0
        while cur != v and steps <= self.m.N:
            nxt = self.next_step(cur, v)
            if nxt < 0:
                steps = 10 ** 9
                break
            cur = nxt
            steps += 1
        self._days[key] = steps
        return steps

    def path_nodes(self, u: int, v: int) -> list[int]:
        """Deterministic movement path from u to v under the judge tie-break."""
        if not self.reachable(u, v):
            return []
        path = [u]
        cur = u
        while cur != v and len(path) <= self.m.N:
            cur = self.next_step(cur, v)
            if cur < 0:
                return []
            path.append(cur)
        return path if path[-1] == v else []

    def prev_step(self, u: int, v: int) -> int:
        """The zone immediately before v on the deterministic path from u."""
        path = self.path_nodes(u, v)
        if len(path) <= 1:
            return u
        return path[-2]


# ───────────────────────── 3. Combat simulator ────────────────────────────────
# Faithful to testing-tool.apply_day_combat + apply_day_siege for one zone.
# A side's number of attacks == (its warriors present at the start of combat) +
# (its building's turret).  Each attack lowers the lowest-hp living enemy warrior
# by 1; attacks that find no living enemy warrior are "idle".  The attacker's
# (non-owner's) idle attacks become siege damage to the building.  0-hp warriors
# retreat only after combat, so they still count toward the attack budget.

def _apply_hits(hps: list[int], hits: int) -> tuple[list[int], int]:
    """Apply `hits` single-point hits, lowest-first. Returns (survivors, idle)."""
    total = sum(hps)
    non_idle = min(hits, total)
    idle = hits - non_idle
    arr = sorted(hps)            # ascending; dead (<=0) entries sink to front
    remaining = non_idle
    while remaining > 0:
        i = 0
        while i < len(arr) and arr[i] <= 0:
            i += 1
        if i == len(arr):
            break
        arr[i] -= 1              # smallest positive stays smallest -> still sorted
        remaining -= 1
    return [h for h in arr if h > 0], idle


def combat_resolve(my_hps: list[int], en_hps: list[int],
                   owner: Optional[str], turret: int, b_hp: int
                   ) -> tuple[list[int], list[int], int, bool]:
    """One day of combat in one zone, from 'my' perspective.
    owner in {'ME','EN',None}. Returns (my_alive, en_alive, b_hp, destroyed)."""
    my = [h for h in my_hps if h > 0]
    en = [h for h in en_hps if h > 0]
    my_cap = len(my) + (turret if owner == "ME" else 0)
    en_cap = len(en) + (turret if owner == "EN" else 0)

    en_after, my_idle = _apply_hits(en, my_cap)   # my attacks hit enemy warriors
    my_after, en_idle = _apply_hits(my, en_cap)   # enemy attacks hit my warriors

    destroyed = False
    if owner is not None and b_hp > 0:
        siege = my_idle if owner == "EN" else en_idle
        b_hp -= min(siege, b_hp)
        if b_hp <= 0:
            destroyed = True
    return my_after, en_after, b_hp, destroyed


def project(my_arrivals: dict[int, list[int]], en_arrivals: dict[int, list[int]],
            owner: str, turret: int, b_hp: int, horizon: int
            ) -> tuple[Optional[int], int]:
    """Simulate up to `horizon` days. Returns (fall_day, final_hp): the day the
    building owned by `owner` is destroyed (None if it survives) and its final
    hp. A surviving building may still have lost hp to chip siege.

    owner == 'ME' -> threat projection (when does MY building fall).
    owner == 'EN' -> attack projection (when does the ENEMY building fall)."""
    # The building's attackers are the side that does NOT own it.
    atk_arrivals = en_arrivals if owner == "ME" else my_arrivals
    my: list[int] = []
    en: list[int] = []
    for d in range(horizon + 1):
        my += my_arrivals.get(d, [])
        en += en_arrivals.get(d, [])
        attackers = en if owner == "ME" else my
        if not attackers:
            if not any(k > d for k in atk_arrivals):
                return None, b_hp    # no one left to attack the building
            continue                 # wait for the attacking force to arrive
        my, en, b_hp, destroyed = combat_resolve(my, en, owner, turret, b_hp)
        if destroyed:
            return d, 0
    return None, b_hp


# ───────────────────────── 4. Risk engine (독립 모듈) ──────────────────────────
class ThreatLevel(Enum):
    NONE = 0
    LOW = 1
    MEDIUM = 2
    HIGH = 3


class OpeningStage(Enum):
    MOVE_FIRST = 1
    BUILD_FIRST = 2
    WAIT_FIRST = 3
    MOVE_SECOND = 4
    BUILD_SECOND = 5
    WAIT_SECOND = 6
    DONE = 7


@dataclass
class Threat:
    region: int
    level: ThreatLevel
    fall_day: Optional[int]            # day building falls with current defence
    attackers: int                     # known enemy warriors that can reach it


@dataclass
class AttackPlan:
    region: int
    attackers: list[str]               # warrior ids to commit
    fall_day: int


class RiskEngine:
    """Simulation-based risk/opportunity assessment — the heart of the bot.

    Threat and attack chances are derived from the *actual* combat rules
    (turret, soldier hp, arrival timing) through the shared `project` simulator,
    never from a flat score, exactly as the spec demands."""

    HIGH_DAY = 2          # falls within 2 days with current defence -> HIGH
    MED_DAY = 5           # falls within 5 days                      -> MEDIUM
    MAX_DEFENDERS = 6     # cap when searching required reinforcements

    def __init__(self, m: GameMap, s: GameState, p: Paths, horizon: int = 14):
        self.m, self.s, self.p = m, s, p
        self.HORIZON = horizon

    # -- helpers ---------------------------------------------------------------
    def _my_warriors(self):
        return [w for w in self.s.warriors.values()
                if w.side is self.m.me and w.hp > 0]

    def _enemy_warriors(self):
        return [w for w in self.s.warriors.values()
                if w.side is not self.m.me and w.hp > 0]

    def _arrivals(self, warriors, zone: int, horizon: int) -> dict[int, list[int]]:
        arr: dict[int, list[int]] = defaultdict(list)
        for w in warriors:
            eta = self.p.days(w.region, zone)
            if eta <= horizon:
                arr[eta].append(w.hp)
        return arr

    def friendly_inbound(self, region: int) -> dict[int, list[int]]:
        """My warriors already moving toward `region` (counted so we neither
        over-pull defenders nor ignore reinforcements already on the way)."""
        arr: dict[int, list[int]] = defaultdict(list)
        for w in self._my_warriors():
            if w.moving and w.target == region:
                eta = self.p.days(w.region, region)
                if 1 <= eta <= self.HORIZON:
                    arr[eta].append(w.hp)
        return arr

    CLOSE_THREAT = 5      # enemies this near always count, even if not advancing

    def _income_of(self, side: Side) -> int:
        total = 0
        for b in self.s.buildings.values():
            if b.side is not side:
                continue
            cnt = sum(1 for w in self.s.warriors.values()
                      if w.side is side and w.hp > 0 and w.region == b.region)
            total += WORK_INCOME * min(cnt, b.work_cap)
        return total

    def intent_score(self, w: Warrior, target: int) -> float:
        """Heuristic probability that an observed enemy is moving toward target."""
        eta_now = self.p.days(w.region, target)
        if eta_now >= 10 ** 9:
            return 0.0
        direction_score = 0.0
        if w.prev_region is not None and self.p.reachable(w.prev_region, target):
            eta_prev = self.p.days(w.prev_region, target)
            if eta_now < eta_prev:
                direction_score = 1.0
            elif eta_now == eta_prev and w.moving:
                direction_score = 0.4
        close_score = max(0.0, 1.0 - eta_now / max(1, self.HORIZON))
        b = self.s.building_at(target)
        if b is None:
            target_value = 0.35
        elif b.type is BType.HQ:
            target_value = 1.0
        else:
            target_value = min(1.0, 0.45 + 0.15 * b.level + 0.05 * b.work_cap)
        return 0.50 * direction_score + 0.30 * close_score + 0.20 * target_value

    def _threat_arrivals(self, region: int) -> dict[int, list[int]]:
        """Enemy warriors that genuinely threaten `region`.

        Besides close enemies, this uses recent movement direction through
        prev_region so a group marching toward a valuable target is counted
        before it is already adjacent.
        """
        hq_eta = self.p.days(self.m.enemy_hq, region)
        arr: dict[int, list[int]] = defaultdict(list)
        for w in self._enemy_warriors():
            eta = self.p.days(w.region, region)
            if eta > self.HORIZON:
                continue
            advancing_from_hq = eta <= hq_eta - 1
            if (eta <= self.CLOSE_THREAT or advancing_from_hq
                    or self.intent_score(w, region) >= 0.50):
                arr[eta].append(w.hp)
        return arr

    def static_defenders(self, zone: int) -> list[int]:
        return [w.hp for w in self._my_warriors() if w.region == zone]

    def my_new_warrior_hp(self) -> int:
        hq = self.s.building_at(self.m.my_hq)
        return HQ_LEVELS[hq.level if hq else 1][1]

    # -- threat ----------------------------------------------------------------
    def simulate_defense(self, region: int, defenders: list[int],
                         extra_today: int = 0,
                         supports: Optional[list[Warrior]] = None
                         ) -> tuple[Optional[int], int]:
        """(fall_day, final_hp) for `region` with support ETA applied."""
        b = self.s.building_at(region)
        if b is None or b.side is not self.m.me:
            return None, 0
        en_arr = self._threat_arrivals(region)
        if not en_arr:
            return None, b.hp
        my_arr: dict[int, list[int]] = defaultdict(list)
        my_arr[0].extend(defenders)
        for d, hps in self.friendly_inbound(region).items():
            my_arr[d].extend(hps)
        for w in supports or []:
            eta = self.p.days(w.region, region)
            if eta <= self.HORIZON:
                my_arr[eta].append(w.hp)
        if extra_today:
            my_arr[0].extend([self.my_new_warrior_hp()] * extra_today)
        return project(my_arr, en_arr, "ME", b.turret, b.hp, self.HORIZON)

    def threat_fall_day(self, region: int, defenders: list[int],
                        extra_today: int = 0,
                        supports: Optional[list[Warrior]] = None) -> Optional[int]:
        return self.simulate_defense(region, defenders, extra_today, supports)[0]

    def defenders_needed(self, region: int, max_g: int = 6,
                         require_intact: bool = False) -> Optional[int]:
        """Absolute number of fresh defenders to keep at `region` today (on top
        of inbound friends). With require_intact, the building must take NO
        siege (vital for the HQ — chip damage loses the hp tiebreak); otherwise
        it need only survive. 0 if unthreatened, None if it cannot be held."""
        b = self.s.building_at(region)
        if b is None or b.side is not self.m.me:
            return 0
        if not self._threat_arrivals(region):
            return 0
        for g in range(max_g + 1):
            fall, hp = self.simulate_defense(region, [], extra_today=g)
            ok = (hp >= b.hp) if require_intact else (fall is None)
            if ok:
                return g
        return None                             # cannot hold even with max_g

    def evaluate_threat(self, region: int) -> Threat:
        defenders = self.static_defenders(region)
        fall = self.threat_fall_day(region, defenders)
        en_arr = self._threat_arrivals(region)
        n_att = sum(len(v) for v in en_arr.values())
        if fall is None:
            lvl = ThreatLevel.NONE
        elif fall <= self.HIGH_DAY:
            lvl = ThreatLevel.HIGH
        elif fall <= self.MED_DAY:
            lvl = ThreatLevel.MEDIUM
        else:
            lvl = ThreatLevel.LOW
        return Threat(region, lvl, fall, n_att)

    def required_defenders(self, region: int) -> Optional[int]:
        """Smallest # of extra warriors placed today that lets the building
        survive the horizon. None if even MAX_DEFENDERS is not enough."""
        defenders = self.static_defenders(region)
        for k in range(self.MAX_DEFENDERS + 1):
            if self.threat_fall_day(region, defenders, extra_today=k) is None:
                return k
        return None

    def support_plan(self, region: int, pool: list[Warrior], *,
                     require_intact: bool = False,
                     max_support: Optional[int] = None) -> Optional[list[Warrior]]:
        """Minimal ETA-aware support list that keeps `region` alive.

        Unlike defenders_needed(), supports are inserted on their actual arrival
        day, so a far-away soldier no longer counts as a same-day defender.
        """
        b = self.s.building_at(region)
        if b is None or b.side is not self.m.me:
            return []
        if not self._threat_arrivals(region):
            return []
        defenders = self.static_defenders(region)
        cands = [w for w in pool if self.p.reachable(w.region, region)]
        cands.sort(key=lambda w: (self.p.days(w.region, region), w.suffix))
        upper = min(len(cands), max_support if max_support is not None else self.MAX_DEFENDERS)
        for k in range(upper + 1):
            supports = cands[:k]
            fall, hp = self.simulate_defense(region, defenders, supports=supports)
            ok = (hp >= b.hp) if require_intact else (fall is None)
            if ok:
                return supports
        return None

    def enemy_hq_training_forecast(self, horizon: int) -> dict[int, list[int]]:
        """Conservative defender forecast: enemy trains max at HQ while we march."""
        arr: dict[int, list[int]] = defaultdict(list)
        hq = self.s.building_at(self.m.enemy_hq)
        if hq is None or hq.side is self.m.me:
            return arr
        gold = self.s.enemy_gold_est
        hp = HQ_LEVELS[hq.level][1]
        cap = hq.train_cap
        enemy_count = len(self._enemy_warriors())
        income = self._income_of(self.m.me.opposite)
        for day in range(1, horizon + 1):
            n = min(cap, gold // TRAIN_COST)
            if n > 0:
                arr[day].extend([hp] * n)
                gold -= n * TRAIN_COST
                enemy_count += n
            gold = max(0, gold + income - UPKEEP_PER_WARRIOR * enemy_count)
        return arr

    # -- attack ----------------------------------------------------------------
    def evaluate_attack(self, region: int, available: list[Warrior],
                        max_attackers: int, horizon: Optional[int] = None,
                        in_transit: Optional[list[tuple[int, int]]] = None
                        ) -> Optional[AttackPlan]:
        """Smallest set of `available` warriors (added to any `in_transit`
        force already heading there) that destroys the enemy building at
        `region` within `horizon`. Enemy defenders are the enemy warriors that
        can reach `region` in time (their march away from it excludes them)."""
        hz = horizon if horizon is not None else self.HORIZON
        b = self.s.building_at(region)
        if b is None or b.side is self.m.me:
            return None
        cands = [w for w in available if self.p.reachable(w.region, region)]
        cands.sort(key=lambda w: self.p.days(w.region, region))
        en_arr = self._arrivals(self._enemy_warriors(), region, hz)
        if region == self.m.enemy_hq:
            for d, hps in self.enemy_hq_training_forecast(hz).items():
                en_arr[d].extend(hps)
        base: dict[int, list[int]] = defaultdict(list)
        for eta, hp in (in_transit or []):
            if eta <= hz:
                base[eta].append(hp)
        upper = min(max_attackers, len(cands))
        for k in range(upper + 1):              # k == 0 -> in_transit alone may do
            my_arr: dict[int, list[int]] = defaultdict(list, {d: list(v) for d, v in base.items()})
            for w in cands[:k]:
                my_arr[self.p.days(w.region, region)].append(w.hp)
            if not my_arr:
                continue
            fall = project(my_arr, en_arr, "EN", b.turret, b.hp, hz)[0]
            if fall is not None:
                return AttackPlan(region, [w.wid for w in cands[:k]], fall)
        return None


# ───────────────────────── 1b. I/O & state tracking ───────────────────────────
def readln() -> str:
    line = sys.stdin.readline()
    if not line:
        sys.exit(0)
    return line.rstrip("\n")


def read_tokens() -> list[str]:
    return readln().split()


def parse_init() -> tuple[GameMap, GameState]:
    """Read the READY block. Does NOT print OK — main() does that after the
    (cheap) path precompute so turn 1 keeps its full time budget."""
    m = GameMap()
    t = read_tokens()
    assert t and t[0] == "READY"
    m.me = Side.from_word(t[1])

    t = read_tokens()
    m.N, m.K = int(t[0]), int(t[1])
    m.x = [int(v) for v in read_tokens()]
    m.y = [int(v) for v in read_tokens()]
    m.sites = sorted(int(v) for v in read_tokens())
    m.site_set = set(m.sites)

    m.adj = [[] for _ in range(m.N)]
    for r in range(m.N):
        t = read_tokens()
        deg = int(t[0])
        m.adj[r] = sorted(int(v) for v in t[1:1 + deg])

    m.my_hq = 0 if m.me is Side.LEFT else m.N - 1
    m.enemy_hq = m.N - 1 if m.me is Side.LEFT else 0

    s = GameState()
    enemy = m.me.opposite
    for sfx in range(1, START_WARRIORS + 1):
        for side, hq in ((m.me, m.my_hq), (enemy, m.enemy_hq)):
            wid = f"{side.value}{sfx}"
            s.warriors[wid] = Warrior(wid, side, sfx, hq, HQ_LEVELS[1][1])
    s.buildings[0] = Building(0, Side.LEFT, BType.HQ, 1, HQ_LEVELS[1][2])
    s.buildings[m.N - 1] = Building(m.N - 1, Side.RIGHT, BType.HQ, 1, HQ_LEVELS[1][2])
    return m, s


def read_turn_start() -> Optional[int]:
    line = readln()
    if line == "FINISH":
        return None
    t = line.split()
    assert t and t[0] == "START"
    return int(t[2])


def _parse_wid(tok: str) -> tuple[str, Side, int]:
    return tok, Side.from_char(tok[0]), int(tok[1:])


def read_turn_result(s: GameState, m: GameMap, submitted: Actions) -> None:
    """Charge our morning spend, then read & apply the day's result so `s`
    mirrors the judge afterwards, and refresh the enemy gold estimate."""
    for region in submitted.upgrades:
        b = s.building_at(region)
        if b is None:
            s.gold -= BASE_BUILD_COST
            s.buildings[region] = Building(region, m.me, BType.BASE, 1, BASE_LEVELS[1][1])
        else:
            s.gold -= b.next_cost()
            b.apply_upgrade_or_heal()
    for wid, target in submitted.moves:
        b = s.building_at(target)
        s.gold -= 0 if (b is not None and b.side is m.me) else MOVE_COST
        w = s.warriors.get(wid)
        if w is not None:
            w.moving = True
            w.target = target
    s.gold -= TRAIN_COST * submitted.train_n

    line = readln()
    if line == "FINISH":
        sys.exit(0)
    assert line.split()[0] == "TURN"

    t = read_tokens()                       # TIME T_x R_x T_y R_y
    s.my_countdown = int(t[2])
    s.enemy_countdown = int(t[4])

    enemy_spend = 0
    prev_moving = {w.wid for w in s.warriors.values()
                   if w.side is not m.me and w.moving}

    # UPGRADE -----------------------------------------------------------------
    n = int(read_tokens()[1])
    for _ in range(n):
        r = read_tokens()
        side = Side.from_char(r[0][0])
        region = int(r[1])
        b = s.building_at(region)
        if side is m.me:
            continue                        # already applied above
        if b is None:
            enemy_spend += BASE_BUILD_COST
            s.buildings[region] = Building(region, side, BType.BASE, 1, BASE_LEVELS[1][1])
        elif b.side is side:
            enemy_spend += b.next_cost()
            b.apply_upgrade_or_heal()

    # TRAIN -------------------------------------------------------------------
    n = int(read_tokens()[1])
    if n > 0:
        ids = read_tokens()
        for tok in ids[:n]:
            wid, side, sfx = _parse_wid(tok)
            hq_region = m.my_hq if side is m.me else m.enemy_hq
            hq = s.building_at(hq_region)
            lvl = hq.level if hq else 1
            s.warriors[wid] = Warrior(wid, side, sfx, hq_region, HQ_LEVELS[lvl][1])
            if side is not m.me:
                enemy_spend += TRAIN_COST

    # MOVE --------------------------------------------------------------------
    n = int(read_tokens()[1])
    moved_now = set()
    for _ in range(n):
        r = read_tokens()
        wid, region = r[0], int(r[1])
        w = s.warriors.get(wid)
        if w is None:
            continue
        w.prev_region = w.region
        w.region = region
        moved_now.add(wid)
        if w.side is m.me:
            if w.moving and w.region == w.target:
                w.moving = False
                w.target = None
        else:
            if wid not in prev_moving:      # fresh enemy move command: 10g
                tb = s.building_at(region)
                if not (tb is not None and tb.side is not m.me):
                    enemy_spend += MOVE_COST
            w.moving = True
    for w in s.warriors.values():
        if w.side is not m.me and w.wid not in moved_now:
            w.moving = False

    # DAMAGE ------------------------------------------------------------------
    n = int(read_tokens()[1])
    for _ in range(n):
        r = read_tokens()
        w = s.warriors.get(r[1])
        if w is not None:
            w.hp -= int(r[2])
    for wid in [wid for wid, w in s.warriors.items() if w.hp <= 0]:
        del s.warriors[wid]

    # SIEGE -------------------------------------------------------------------
    n = int(read_tokens()[1])
    for _ in range(n):
        r = read_tokens()
        b = s.building_at(int(r[1]))
        if b is not None:
            b.hp -= int(r[2])
    for reg in [reg for reg, b in s.buildings.items() if b.hp <= 0]:
        del s.buildings[reg]

    readln()                                # END

    # --- evening economy (mirror simulator) for both sides ---
    def income_of(side: Side) -> int:
        total = 0
        for b in s.buildings.values():
            if b.side is not side:
                continue
            cnt = sum(1 for w in s.warriors.values()
                      if w.side is side and w.region == b.region)
            total += WORK_INCOME * min(cnt, b.work_cap)
        return total

    s.gold += income_of(m.me)
    my_alive = sum(1 for w in s.warriors.values() if w.side is m.me)
    s.gold = max(0, s.gold - UPKEEP_PER_WARRIOR * my_alive)

    enemy = m.me.opposite
    enemy_alive = sum(1 for w in s.warriors.values() if w.side is enemy)
    s.enemy_gold_est = max(
        0, s.enemy_gold_est + income_of(enemy) - enemy_spend
        - UPKEEP_PER_WARRIOR * enemy_alive)


def emit(a: Actions) -> None:
    out = ["COMMAND"]
    for region in a.upgrades:
        out.append(f"UPGRADE {region}")
    for wid, target in a.moves:
        out.append(f"MOVE {wid} {target}")
    if a.train_n > 0:
        out.append(f"TRAIN {a.train_n}")
    out.append("END")
    sys.stdout.write("\n".join(out) + "\n")
    sys.stdout.flush()


# ───────────────────────── 5..8. Strategy brain ───────────────────────────────
# Priorities for the final gold-budgeted command selection (higher = keep first).
P_HQ_CONSTRUCT = 100   # heal / upgrade HQ to survive
P_HQ_DEFEND = 95       # pull warriors to defend HQ
P_HQ_TRAIN = 90        # train bodies for same-day HQ defence
P_BASE_DEFEND = 80     # defend an important base
P_BASE_CONSTRUCT = 70  # upgrade a threatened base for extra hp
P_BUILD = 60           # build a base on a site we already hold
P_EXPAND = 56          # claim central sites after the opening
P_UPGRADE_ECON = 52    # economic upgrade of a valuable building
P_ATTACK = 48          # send an attack force
P_TRAIN_GROW = 36      # train for growth after the second base
P_WORKFILL = 30        # bring a warrior to staff a building
P_IDLE = 20            # anti-idle relocation

CRITICAL_PRIO = P_BASE_DEFEND   # >= this is never trimmed by the food guard


@dataclass
class Cand:
    prio: int
    kind: str               # 'move' | 'upgrade' | 'train'
    wid: Optional[str] = None
    region: Optional[int] = None
    cost: int = 0


class Brain:
    EARLY_TURN = 30
    LATE_TURN = 150
    MAX_BASES_TARGET = 6
    MAX_EXPAND_PER_TURN = 1
    MAX_PENDING_UNBUILT_SITES = 1
    NEW_BASE_BUFFER = 20
    POST_SECOND_BASE_TRAIN_CAP = 5
    MID_PASSIVE_TRAIN_CAP = 7
    LATE_PASSIVE_TRAIN_CAP = 10
    MIN_EXPAND_WARRIORS = 5
    MIN_HQ_ATTACK_GROUP = 4
    BUILD_RESERVE = 120         # gold kept after building a base
    UPGRADE_RESERVE = 200       # gold kept after an economic upgrade

    def __init__(self, m: GameMap, s: GameState, p: Paths):
        self.m, self.s, self.p = m, s, p
        md = p.days(m.my_hq, m.enemy_hq)
        if md >= 10 ** 9:
            md = m.N
        # horizon long enough to plan/detect a cross-map move and a siege
        self.horizon = max(14, min(md + 8, 40))
        self.risk = RiskEngine(m, s, p, self.horizon)
        self.cx, self.cy = 0.0, 0.0          # battlefield centre (origin)

    # -- accessors -------------------------------------------------------------
    def my_warriors(self):
        return [w for w in self.s.warriors.values()
                if w.side is self.m.me and w.hp > 0]

    def enemy_warriors(self):
        return [w for w in self.s.warriors.values()
                if w.side is not self.m.me and w.hp > 0]

    def my_buildings(self):
        return [b for b in self.s.buildings.values() if b.side is self.m.me]

    def my_bases(self):
        return [b for b in self.my_buildings() if b.type is BType.BASE]

    def enemy_buildings(self):
        return [b for b in self.s.buildings.values() if b.side is not self.m.me]

    def warriors_at(self, region: int, side: Side) -> list[Warrior]:
        return [w for w in self.s.warriors.values()
                if w.hp > 0 and w.side is side and w.region == region]

    def enemy_at(self, region: int) -> bool:
        return any(self.warriors_at(region, self.m.me.opposite))

    def income(self, side: Side) -> int:
        total = 0
        for b in self.s.buildings.values():
            if b.side is not side:
                continue
            total += WORK_INCOME * min(len(self.warriors_at(b.region, side)), b.work_cap)
        return total

    # -- warrior classification ------------------------------------------------
    def employed(self) -> set[str]:
        """Building workers (up to work_cap) kept in place to preserve income.
        HQ defence is handled separately by threat-sized reservation, so it is
        not part of this set — that lets the whole army attack when safe."""
        emp: set[str] = set()
        for b in self.my_buildings():
            here = sorted(self.warriors_at(b.region, self.m.me), key=lambda w: w.suffix)
            for w in here[:b.work_cap]:
                emp.add(w.wid)
        return emp

    def is_settling(self, w: Warrior) -> bool:
        """Standing on an empty site with no enemy -> about to build a base."""
        return (w.region in self.m.site_set
                and self.s.building_at(w.region) is None
                and not self.enemy_at(w.region))

    def free_warriors(self, claimed_w: set[str]) -> list[Warrior]:
        emp = self.employed()
        out = []
        for w in self.my_warriors():
            if w.moving or w.wid in claimed_w or w.wid in emp:
                continue
            if self.is_settling(w):
                continue                          # keep site holders available to build
            if self.enemy_at(w.region):
                continue                          # stuck fighting; a move won't run
            out.append(w)
        return out

    def defender_pool(self, claimed_w: set[str], region: int) -> list[Warrior]:
        """All warriors we could send to defend `region` (free first, then
        employed as a fallback), nearest first, excluding those already there."""
        free = self.free_warriors(claimed_w)
        free_ids = {w.wid for w in free}
        others = [w for w in self.my_warriors()
                  if w.wid not in claimed_w and not w.moving
                  and not self.enemy_at(w.region) and w.wid not in free_ids
                  and w.region != region]
        pool = [w for w in free if w.region != region] + others
        pool.sort(key=lambda w: self.p.days(w.region, region))
        return pool

    # -- economy & phase -------------------------------------------------------
    def classify_phase(self, turn: int) -> str:
        if turn <= self.EARLY_TURN:
            return "EARLY"
        if turn >= self.LATE_TURN:
            return "LATE"
        return "MID"

    def food_reserve(self, extra_train: int = 0) -> int:
        return UPKEEP_PER_WARRIOR * (len(self.my_warriors()) + extra_train)

    def can_spend_noncritical(self, current_gold: int, cost: int,
                              extra_train: int = 0) -> bool:
        return current_gold - cost >= self.food_reserve(extra_train)

    def projected_gold_after_days(self, days: int) -> int:
        gold = self.s.gold
        warriors = len(self.my_warriors())
        for _ in range(days):
            gold += self.income(self.m.me)
            gold -= UPKEEP_PER_WARRIOR * warriors
            gold = max(0, gold)
        return gold

    def can_afford_base_by_arrival(self, eta: int) -> bool:
        return self.projected_gold_after_days(eta) >= BASE_BUILD_COST + self.food_reserve()

    def pending_unbuilt_zones(self, include_moving: bool = True) -> set[int]:
        zones: set[int] = set()
        for w in self.my_warriors():
            if self.is_settling(w):
                zones.add(w.region)
            if include_moving and w.moving and w.target in self.m.site_set:
                if self.s.building_at(w.target) is None:
                    zones.add(w.target)
        return zones

    def buildable_pending_zones(self) -> set[int]:
        return {w.region for w in self.my_warriors() if self.is_settling(w)}

    def inbound_pending_zones(self) -> set[int]:
        zones: set[int] = set()
        for w in self.my_warriors():
            if w.moving and w.target in self.m.site_set:
                if self.s.building_at(w.target) is None:
                    zones.add(w.target)
        return zones

    def pending_build_debt(self) -> int:
        return BASE_BUILD_COST * len(self.pending_unbuilt_zones())

    def expansion_locked_by_pending(self) -> bool:
        return bool(self.pending_unbuilt_zones())

    def can_start_new_base_move(self, move_cost: int = MOVE_COST,
                                extra_train: int = 0,
                                current_gold: Optional[int] = None) -> bool:
        if self.pending_unbuilt_zones():
            return False
        gold = self.s.gold if current_gold is None else current_gold
        required = BASE_BUILD_COST + self.food_reserve(extra_train) + self.NEW_BASE_BUFFER
        return gold - move_cost >= required

    # -- opening state machine -------------------------------------------------
    def opening_stage(self) -> OpeningStage:
        base_count = len(self.my_bases())
        settlers = [w for w in self.my_warriors() if self.is_settling(w)]
        moving_to_empty_site = [
            w for w in self.my_warriors()
            if w.moving and w.target in self.m.site_set
            and self.s.building_at(w.target) is None
        ]

        if base_count == 0:
            if settlers:
                return OpeningStage.BUILD_FIRST
            if moving_to_empty_site:
                return OpeningStage.WAIT_FIRST
            return OpeningStage.MOVE_FIRST

        if base_count == 1:
            if settlers:
                return OpeningStage.BUILD_SECOND
            if moving_to_empty_site:
                return OpeningStage.WAIT_SECOND
            return OpeningStage.MOVE_SECOND

        return OpeningStage.DONE

    def first_base_site(self) -> Optional[int]:
        candidates = []
        for z in self.m.sites:
            if self.s.building_at(z) is not None:
                continue
            if not self.p.reachable(self.m.my_hq, z):
                continue
            my_eta = self.p.days(self.m.my_hq, z)
            enemy_eta = self.p.days(self.m.enemy_hq, z)
            if my_eta > enemy_eta:
                continue
            candidates.append((my_eta, self.centrality(z), z))
        if not candidates:
            return None
        candidates.sort()
        return candidates[0][2]

    def _best_opening_worker(self, site: int, claimed_w: set[str]) -> Optional[Warrior]:
        free = [w for w in self.free_warriors(claimed_w)
                if self.p.reachable(w.region, site)]
        if not free:
            free = [w for w in self.my_warriors()
                    if not w.moving and w.wid not in claimed_w
                    and not self.is_settling(w) and not self.enemy_at(w.region)
                    and self.p.reachable(w.region, site)]
        if not free:
            return None
        return min(free, key=lambda w: (self.p.days(w.region, site), w.suffix))

    def _best_second_worker(self, claimed_w: set[str]) -> Optional[Warrior]:
        free = self.free_warriors(claimed_w)
        if not free:
            free = [w for w in self.my_warriors()
                    if not w.moving and w.wid not in claimed_w
                    and not self.is_settling(w) and not self.enemy_at(w.region)]
        if not free:
            return None
        return min(free, key=lambda w: (self.p.days(w.region, self.m.my_hq), w.suffix))

    def second_base_site_for(self, w: Warrior) -> Optional[int]:
        candidates = []
        claimed = self.claimed_sites()
        for z in self.m.sites:
            if z in claimed:
                continue
            if self.s.building_at(z) is not None:
                continue
            if not self.p.reachable(w.region, z):
                continue
            my_eta = self.p.days(w.region, z)
            enemy_eta = self.p.days(self.m.enemy_hq, z)
            if my_eta > enemy_eta:
                continue
            candidates.append((my_eta, self.centrality(z), z))
        if not candidates:
            return None
        candidates.sort()
        return candidates[0][2]

    def _build_opening_base(self, cands, used_regions, claimed_w):
        for w in sorted(self.my_warriors(), key=lambda x: x.suffix):
            if not self.is_settling(w):
                continue
            z = w.region
            if z in used_regions:
                continue
            if self.s.building_at(z) is not None:
                continue
            if self.enemy_at(z):
                continue
            if not self.can_spend_noncritical(self.s.gold, BASE_BUILD_COST):
                continue
            cands[:] = [c for c in cands if not (c.kind == "move" and c.wid == w.wid)]
            cands.append(Cand(P_BUILD, "upgrade", region=z, cost=BASE_BUILD_COST))
            used_regions.add(z)
            claimed_w.add(w.wid)
            break

    def _plan_opening(self, cands, claimed_w, used_regions) -> bool:
        stage = self.opening_stage()
        if stage is OpeningStage.DONE:
            return False

        if stage in (OpeningStage.BUILD_FIRST, OpeningStage.BUILD_SECOND):
            self._build_opening_base(cands, used_regions, claimed_w)
            return True

        if stage is OpeningStage.MOVE_FIRST:
            site = self.first_base_site()
            if site is None:
                return True
            w = self._best_opening_worker(site, claimed_w)
            if w is None:
                return True
            if self.can_start_new_base_move(MOVE_COST):
                cands.append(Cand(P_EXPAND, "move", wid=w.wid, region=site, cost=MOVE_COST))
                claimed_w.add(w.wid)
            return True

        if stage is OpeningStage.MOVE_SECOND:
            if self.pending_unbuilt_zones():
                return True
            w = self._best_second_worker(claimed_w)
            if w is None:
                return True
            site = self.second_base_site_for(w)
            if site is None:
                return True
            if self.can_start_new_base_move(MOVE_COST):
                cands.append(Cand(P_EXPAND, "move", wid=w.wid, region=site, cost=MOVE_COST))
                claimed_w.add(w.wid)
            return True

        return True

    # -- site scoring (spec 1.1 & 1.4) ----------------------------------------
    def centrality(self, zone: int) -> float:
        return math.hypot(self.m.x[zone] - self.cx, self.m.y[zone] - self.cy)

    def site_score(self, zone: int) -> float:
        my_eta = self.p.days(self.m.my_hq, zone)
        en_eta = self.p.days(self.m.enemy_hq, zone)
        # Mid-game expansion values quick, defensible income.
        score = -my_eta * 5.0
        score += min(en_eta - my_eta, 4) * 2.0
        score -= abs(my_eta - en_eta) * 1.0
        score -= self.centrality(zone) / 4000.0
        return score

    def central_site_score(self, zone: int) -> float:
        my_eta = self.p.days(self.m.my_hq, zone)
        en_eta = self.p.days(self.m.enemy_hq, zone)
        score = -self.centrality(zone) / 1000.0
        score -= my_eta * 3.0
        score += min(en_eta - my_eta, 5) * 2.0
        if my_eta > en_eta + 1:
            score -= 100.0
        return score

    def central_candidate_sites(self) -> list[int]:
        claimed = self.claimed_sites()
        out = []
        for z in self.m.sites:
            if z in claimed:
                continue
            if self.s.building_at(z) is not None:
                continue
            if not self.p.reachable(self.m.my_hq, z):
                continue
            my_eta = self.p.days(self.m.my_hq, z)
            en_eta = self.p.days(self.m.enemy_hq, z)
            if my_eta > en_eta + 1:
                continue
            out.append(z)
        out.sort(key=self.central_site_score, reverse=True)
        return out

    def early_site_score(self, zone: int) -> float:
        my_eta = self.p.days(self.m.my_hq, zone)
        en_eta = self.p.days(self.m.enemy_hq, zone)
        diff = abs(my_eta - en_eta)
        contested = 100.0 if diff <= 1 else 30.0 if diff <= 2 else 0.0
        advantage = max(-20.0, min(20.0, (en_eta - my_eta) * 5.0))
        return contested - my_eta * 5.0 - self.centrality(zone) / 3000.0 + advantage

    def expansion_destination(self, site: int) -> int:
        my_eta = self.p.days(self.m.my_hq, site)
        en_eta = self.p.days(self.m.enemy_hq, site)
        if en_eta < my_eta and abs(my_eta - en_eta) <= 1:
            return self.p.prev_step(self.m.my_hq, site)
        return site

    def expansion_pool(self, claimed_w: set[str], phase: str) -> list[Warrior]:
        if phase != "EARLY":
            return self.free_warriors(claimed_w)
        pool = []
        for w in self.my_warriors():
            if (w.moving or w.wid in claimed_w or self.is_settling(w)
                    or self.enemy_at(w.region)):
                continue
            pool.append(w)
        return pool

    def claimed_sites(self) -> set[int]:
        claimed = {b.region for b in self.my_bases()}
        for w in self.my_warriors():
            if w.region in self.m.site_set and self.s.building_at(w.region) is None:
                claimed.add(w.region)
            if w.moving and w.target in self.m.site_set:
                claimed.add(w.target)
        return claimed

    def candidate_sites(self, *, early: bool = False) -> list[int]:
        claimed = self.claimed_sites()
        out = []
        for z in self.m.sites:
            if z in claimed or not self.p.reachable(self.m.my_hq, z):
                continue
            b = self.s.building_at(z)
            if b is not None:                       # already a building here
                continue
            my_eta = self.p.days(self.m.my_hq, z)
            en_eta = self.p.days(self.m.enemy_hq, z)
            # Early play still stages for +/-1 contested sites, but skips lost races.
            if my_eta > en_eta + 1:
                continue
            out.append(z)
        out.sort(key=self.early_site_score if early else self.site_score, reverse=True)
        return out

    def base_value(self, b: Building) -> float:
        return b.level * 10.0 + b.work_cap * 5.0 - self.centrality(b.region) / 4000.0

    def worth_keeping(self, b: Building) -> bool:
        if b.type is BType.HQ or b.level >= 2:
            return True
        return self.centrality(b.region) < self._median_site_centrality()

    def _median_site_centrality(self) -> float:
        if not self.m.sites:
            return 1e18
        vals = sorted(self.centrality(z) for z in self.m.sites)
        return vals[len(vals) // 2]

    # -- main decision (spec section 16) --------------------------------------
    def decide(self, turn: int) -> Actions:
        cands: list[Cand] = []
        claimed_w: set[str] = set()
        used_regions: set[int] = set()

        phase = self.classify_phase(turn)
        behind_econ = self.income(self.m.me) < self.income(self.m.me.opposite) - 30

        hq = self.s.building_at(self.m.my_hq)
        hq_fall = (self.risk.threat_fall_day(self.m.my_hq,
                                             self.risk.static_defenders(self.m.my_hq))
                   if hq else None)
        hq_imminent = hq_fall is not None and hq_fall <= RiskEngine.MED_DAY

        # ---------- 1. HQ defence: reserve a threat-sized garrison ----------
        if hq:
            self._reserve_hq_defense(cands, claimed_w, used_regions,
                                     hq, hq_fall, hq_imminent)

        # ---------- 2. base defence ----------
        abandoned_base = False
        for b in sorted(self.my_bases(), key=self.base_value, reverse=True):
            th = self.risk.evaluate_threat(b.region)
            if th.level in (ThreatLevel.HIGH, ThreatLevel.MEDIUM) and self.worth_keeping(b):
                defended = self._defend_building(b.region, cands, claimed_w, used_regions,
                                                 move_prio=P_BASE_DEFEND,
                                                 build_prio=P_BASE_CONSTRUCT, is_hq=False,
                                                 construct=True)
                if not defended:
                    abandoned_base = True

        # ---------- 3. two-base opening gate ----------
        opening_handled = False
        if not hq_imminent:
            opening_handled = self._plan_opening(cands, claimed_w, used_regions)

        # ---------- 4. build already-held empty sites before any growth ----------
        self._build_on_held_sites(cands, used_regions, claimed_w)

        # ---------- 5. pending debt lock ----------
        pending_lock = self.expansion_locked_by_pending()
        if pending_lock:
            if abandoned_base and not hq_imminent:
                self._plan_counterattack_weakest_base(cands, claimed_w)
            self._staff_and_idle(cands, claimed_w, opening_handled=True)
            actions = self._select(cands)
            if DEBUG:
                g = self.risk.defenders_needed(self.m.my_hq)
                etas = sorted(self.p.days(w.region, self.m.my_hq) for w in self.enemy_warriors())
                md = self.p.days(self.m.enemy_hq, self.m.my_hq)
                dbg(f"T{turn} gold={self.s.gold} w={len(self.my_warriors())} "
                    f"atHQ={len(self.warriors_at(self.m.my_hq, self.m.me))} "
                    f"g={g} fall={hq_fall} bld={len(self.my_buildings())} "
                    f"pending={sorted(self.pending_unbuilt_zones())} "
                    f"hz={self.risk.HORIZON} md={md} en_eta={etas[:5]} "
                    f"mv={actions.moves} up={actions.upgrades} tr={actions.train_n}")
            return actions

        # ---------- 6. post-opening economy / attack / expansion ----------
        if not opening_handled:
            self._plan_growth_training(cands, phase, behind_econ)
            self._plan_expansion(cands, claimed_w, phase)
            self._plan_upgrades(cands, used_regions, phase)
            if abandoned_base and not hq_imminent:
                self._plan_counterattack_weakest_base(cands, claimed_w)
            if behind_econ and not hq_imminent:
                self._plan_attacks(cands, claimed_w, aggressive=True)
                self._plan_hq_attack(cands, claimed_w, phase)
            else:
                self._plan_hq_attack(cands, claimed_w, phase)
                self._plan_attacks(cands, claimed_w, aggressive=(phase == "LATE"))

        # ---------- 7. staffing & anti-idle ----------
        self._staff_and_idle(cands, claimed_w, opening_handled=opening_handled)

        # ---------- 8. budget + food validation ----------
        actions = self._select(cands)
        if DEBUG:
            g = self.risk.defenders_needed(self.m.my_hq)
            etas = sorted(self.p.days(w.region, self.m.my_hq) for w in self.enemy_warriors())
            md = self.p.days(self.m.enemy_hq, self.m.my_hq)
            dbg(f"T{turn} gold={self.s.gold} w={len(self.my_warriors())} "
                f"atHQ={len(self.warriors_at(self.m.my_hq, self.m.me))} "
                f"g={g} fall={hq_fall} bld={len(self.my_buildings())} "
                f"hz={self.risk.HORIZON} md={md} en_eta={etas[:5]} "
                f"mv={actions.moves} up={actions.upgrades} tr={actions.train_n}")
        return actions

    # -- phase helpers ---------------------------------------------------------
    def _add_construct(self, b, cands, used_regions, build_prio, *, allow_heal):
        """Queue a defensive upgrade/heal for a building we hold uncontested."""
        if (b.region in used_regions or self.enemy_at(b.region)
                or not self.warriors_at(b.region, self.m.me)):
            return
        if b.level < b.max_level:
            cands.append(Cand(build_prio, "upgrade", region=b.region, cost=b.next_cost()))
            used_regions.add(b.region)
        elif allow_heal and b.hp < b.max_hp:
            cands.append(Cand(build_prio, "upgrade", region=b.region, cost=b.heal_cost))
            used_regions.add(b.region)

    def _reserve_hq_defense(self, cands, claimed_w, used_regions, hq, hq_fall, imminent):
        """Keep exactly as many warriors at the HQ as the *advancing* enemy
        force requires (0 when the enemy is spent -> the army is freed to
        attack), pulling extra ones home and reinforcing same-day if imminent."""
        g = self.risk.defenders_needed(self.m.my_hq, require_intact=True)
        unholdable = g is None                    # HQ is never abandoned
        if unholdable:
            g = RiskEngine.MAX_DEFENDERS          # keep as many as we can
        # keep at least one body home while any enemy is near, so a straggler
        # hits a warrior instead of chipping HQ hp (which loses the tiebreak)
        near = any(self.p.days(w.region, self.m.my_hq) <= RiskEngine.CLOSE_THREAT
                   for w in self.enemy_warriors())
        if near:
            g = max(g, 1)
        if not imminent and not unholdable and len(self.my_bases()) < 2:
            g = min(g, 1)
        at_hq = sorted(self.warriors_at(self.m.my_hq, self.m.me), key=lambda w: w.suffix)
        for w in at_hq[:g]:                       # hold these in place (no command)
            claimed_w.add(w.wid)
        need_more = max(0, g - len(at_hq[:g]))
        may_pull_support = imminent or unholdable or len(self.my_bases()) >= 2
        if need_more > 0 and may_pull_support:
            pool = self.defender_pool(claimed_w, self.m.my_hq)
            if imminent:
                support = self.risk.support_plan(self.m.my_hq, pool, require_intact=True,
                                                 max_support=RiskEngine.MAX_DEFENDERS)
                if support is None:
                    unholdable = True
                    support = pool[:need_more]
            else:
                support = pool[:need_more]
            for w in support:
                cands.append(Cand(P_HQ_DEFEND, "move", wid=w.wid, region=self.m.my_hq, cost=0))
                claimed_w.add(w.wid)
        if imminent or unholdable:
            self._add_construct(hq, cands, used_regions, P_HQ_CONSTRUCT, allow_heal=True)
            if (hq_fall is not None and hq_fall <= RiskEngine.HIGH_DAY) or unholdable:
                for _ in range(hq.train_cap):
                    cands.append(Cand(P_HQ_TRAIN, "train", cost=TRAIN_COST))

    def _defend_building(self, region, cands, claimed_w, used_regions, *,
                         move_prio, build_prio, is_hq, construct=True) -> bool:
        b = self.s.building_at(region)
        if b is None:
            return False
        pool = self.defender_pool(claimed_w, region)
        support = self.risk.support_plan(region, pool, require_intact=is_hq,
                                         max_support=RiskEngine.MAX_DEFENDERS)
        if support is None:
            return False                           # spec 15.4: cannot hold -> abandon
        if construct:
            self._add_construct(b, cands, used_regions, build_prio, allow_heal=is_hq)
        at_region = sorted(self.warriors_at(region, self.m.me), key=lambda w: w.suffix)
        for w in at_region:                        # threatened building: keep local bodies
            claimed_w.add(w.wid)
        for w in support:
            cands.append(Cand(move_prio, "move", wid=w.wid, region=region, cost=0))
            claimed_w.add(w.wid)
        return True

    def _build_on_held_sites(self, cands, used_regions, claimed_w):
        if len(self.my_bases()) >= self.MAX_BASES_TARGET:
            return
        for z in sorted(self.buildable_pending_zones()):
            if z in used_regions:
                continue
            if self.s.building_at(z) is not None:
                continue
            if self.enemy_at(z):
                continue
            if not self.can_spend_noncritical(self.s.gold, BASE_BUILD_COST):
                continue
            here = self.warriors_at(z, self.m.me)
            if not here:
                continue
            cands.append(Cand(P_BUILD, "upgrade", region=z, cost=BASE_BUILD_COST))
            used_regions.add(z)
            claimed_w.add(min(here, key=lambda w: w.suffix).wid)
            break

    def threat_score(self, th: Threat) -> float:
        base = {
            ThreatLevel.NONE: 0.0,
            ThreatLevel.LOW: 25.0,
            ThreatLevel.MEDIUM: 60.0,
            ThreatLevel.HIGH: 90.0,
        }[th.level]
        if th.fall_day is not None:
            base += max(0.0, 10.0 - th.fall_day)
        return min(100.0, base)

    def upgrade_value(self, b: Building, phase: str) -> float:
        old_th = self.risk.evaluate_threat(b.region)
        old_score = self.threat_score(old_th)
        old_level, old_hp, old_work = b.level, b.hp, b.work_cap
        workers_here = len(self.warriors_at(b.region, self.m.me))
        b.level += 1
        b.hp = b.max_hp
        new_work = b.work_cap
        new_score = self.threat_score(self.risk.evaluate_threat(b.region))
        b.level, b.hp = old_level, old_hp
        income_gain = WORK_INCOME * max(
            0, min(workers_here, new_work) - min(workers_here, old_work)
        )
        central = 1.0 / (1.0 + self.centrality(b.region) / 5000.0)
        late_hq = 1.0 if b.type is BType.HQ and phase == "LATE" else 0.0
        return (40.0 * max(0.0, old_score - new_score) / 100.0
                + 30.0 * min(1.0, income_gain / WORK_INCOME)
                + 20.0 * central
                + 10.0 * late_hq
                + (8.0 if b.type is BType.HQ else 0.0))

    def _plan_upgrades(self, cands, used_regions, phase):
        if self.pending_unbuilt_zones():
            return
        targets = []
        for b in self.my_buildings():
            if b.region in used_regions or self.enemy_at(b.region):
                continue
            if not self.warriors_at(b.region, self.m.me) or b.level >= b.max_level:
                continue
            if self.s.gold < b.next_cost() + self.UPGRADE_RESERVE:
                continue
            if self.risk.evaluate_threat(b.region).level in (
                    ThreatLevel.HIGH, ThreatLevel.MEDIUM):
                continue                              # handled by defence logic
            score = self.upgrade_value(b, phase)
            targets.append((score, b))
        targets.sort(key=lambda t: t[0], reverse=True)
        for _, b in targets:
            cands.append(Cand(P_UPGRADE_ECON, "upgrade", region=b.region, cost=b.next_cost()))
            used_regions.add(b.region)

    def _plan_expansion(self, cands, claimed_w, phase: str):
        if len(self.my_bases()) < 2:
            return
        if self.expansion_locked_by_pending():
            return
        if len(self.my_warriors()) < 5:
            return
        if not self.can_start_new_base_move(MOVE_COST):
            return

        max_expand = self.MAX_EXPAND_PER_TURN
        sites = self.central_candidate_sites()[:max_expand]
        free = self.free_warriors(claimed_w)
        assigned = 0
        for z in sites:
            if assigned >= max_expand or not free:
                break
            free.sort(key=lambda w: self.p.days(w.region, z))
            w = free.pop(0)
            if z == w.region:
                continue
            cands.append(Cand(P_EXPAND, "move", wid=w.wid, region=z, cost=MOVE_COST))
            claimed_w.add(w.wid)
            assigned += 1

    def weakest_enemy_base_score(self, b: Building) -> float:
        defenders = len(self.warriors_at(b.region, self.m.me.opposite))
        hp_frac = b.hp / max(1, b.max_hp)
        dist = min((self.p.days(w.region, b.region) for w in self.my_warriors()), default=10 ** 9)
        return 50.0 / (1 + defenders) + 30.0 * (1.0 - hp_frac) + 20.0 / (1 + dist)

    def _plan_counterattack_weakest_base(self, cands, claimed_w):
        free = self.free_warriors(claimed_w)
        if not free:
            return
        targets = [b for b in self.enemy_buildings() if b.type is BType.BASE]
        targets.sort(key=self.weakest_enemy_base_score, reverse=True)
        for b in targets:
            plan = self.risk.evaluate_attack(b.region, free, len(free))
            if plan is None or not plan.attackers:
                continue
            committed = set(plan.attackers)
            remaining = [w for w in self.my_warriors()
                         if w.wid not in committed and not w.moving]
            if not self._hq_safe_without(remaining):
                continue
            for wid in plan.attackers:
                cands.append(Cand(P_BASE_CONSTRUCT, "move", wid=wid, region=b.region,
                                  cost=MOVE_COST))
                claimed_w.add(wid)
            break

    def _plan_attacks(self, cands, claimed_w, *, aggressive):
        if self.pending_unbuilt_zones():
            return
        free = self.free_warriors(claimed_w)
        if not free:
            return
        reserve = 0 if aggressive else 1
        if len(free) - reserve <= 0:
            return
        max_attackers = len(free) - (0 if aggressive else reserve)
        # Generic raids hit bases only; enemy HQ assaults go through
        # _plan_hq_attack, where minimum group size is enforced.
        targets = sorted((b for b in self.enemy_buildings() if b.type is BType.BASE),
                         key=lambda b: (b.hp, self.centrality(b.region)))
        for b in targets:
            plan = self.risk.evaluate_attack(b.region, free, max_attackers)
            if plan is None:
                continue
            committed = set(plan.attackers)
            remaining = [w for w in self.my_warriors()
                         if w.wid not in committed and not w.moving]
            if not self._hq_safe_without(remaining):
                continue
            tb = self.s.building_at(b.region)
            for wid in plan.attackers:
                cost = 0 if (tb is not None and tb.side is self.m.me) else MOVE_COST
                cands.append(Cand(P_ATTACK, "move", wid=wid, region=b.region, cost=cost))
                claimed_w.add(wid)
            break                                   # one coordinated attack per turn

    def _plan_hq_attack(self, cands, claimed_w, phase):
        """Strategic win condition: march a sufficient strike force on the enemy
        HQ once we are economically stable. Uses a long horizon (a cross-map
        siege far exceeds the tactical horizon) and counts warriors already en
        route so we top up rather than over-commit. Safety is delegated to the
        HQ reservation (committed warriors come only from the free surplus) plus
        the explicit _hq_safe_without check below."""
        if self.s.building_at(self.m.enemy_hq) is None:
            return                                   # already destroyed
        n = len(self.my_warriors())
        if self.income(self.m.me) - UPKEEP_PER_WARRIOR * n < 0:
            return                                   # can't sustain a march
        free = self.free_warriors(claimed_w)
        if not free:
            return
        in_transit_warriors = [w for w in self.my_warriors()
                               if w.moving and w.target == self.m.enemy_hq]
        in_transit = [(self.p.days(w.region, self.m.enemy_hq), w.hp)
                      for w in in_transit_warriors]
        horizon = self.p.days(self.m.my_hq, self.m.enemy_hq) + 25
        plan = self.risk.evaluate_attack(self.m.enemy_hq, free, len(free),
                                         horizon=horizon, in_transit=in_transit)
        if plan is None or not plan.attackers:
            return
        enemy_hq_defenders = len(self.warriors_at(self.m.enemy_hq, self.m.me.opposite))
        min_group = max(self.MIN_HQ_ATTACK_GROUP, enemy_hq_defenders + 2)
        if len(plan.attackers) + len(in_transit_warriors) < min_group:
            return
        committed = set(plan.attackers)
        remaining = [w for w in self.my_warriors()
                     if w.wid not in committed
                     and not (w.moving and w.target == self.m.enemy_hq)]
        if not self._hq_safe_without(remaining):
            return
        for wid in plan.attackers:
            cands.append(Cand(P_ATTACK, "move", wid=wid, region=self.m.enemy_hq,
                              cost=MOVE_COST))
            claimed_w.add(wid)

    def _hq_safe_without(self, defenders: list[Warrior]) -> bool:
        hq = self.s.building_at(self.m.my_hq)
        if hq is None:
            return False
        hp_at_hq = [w.hp for w in defenders if w.region == self.m.my_hq]
        fall, final_hp = self.risk.simulate_defense(self.m.my_hq, hp_at_hq)
        return fall is None and final_hp >= hq.hp   # HQ must stay fully intact

    def _fill_existing_base_worker_only(self, cands, claimed_w):
        open_bases = []
        for b in self.my_bases():
            slots = b.work_cap - len(self.warriors_at(b.region, self.m.me))
            if slots > 0:
                open_bases.append(b)
        if not open_bases:
            return
        free = self.free_warriors(claimed_w)
        if not free:
            return
        best = None
        for w in free:
            for b in open_bases:
                if w.region == b.region or not self.p.reachable(w.region, b.region):
                    continue
                item = (self.p.days(w.region, b.region), w.suffix, w, b)
                if best is None or item[:2] < best[:2]:
                    best = item
        if best is None:
            return
        _, _, w, b = best
        cands.append(Cand(P_WORKFILL, "move", wid=w.wid, region=b.region, cost=0))
        claimed_w.add(w.wid)

    def _staff_and_idle(self, cands, claimed_w, opening_handled=False):
        """Fill work slots after the opening; before the second base, only
        allow one free move into an existing base worker slot."""
        if opening_handled and len(self.my_bases()) < 2:
            self._fill_existing_base_worker_only(cands, claimed_w)
            return

        open_slots: list[list[int]] = []            # [region, slots]
        for b in self.my_buildings():
            slots = b.work_cap - len(self.warriors_at(b.region, self.m.me))
            if slots > 0:
                open_slots.append([b.region, slots])
        free = self.free_warriors(claimed_w)
        for w in free:
            if not open_slots:
                break
            open_slots.sort(key=lambda rs: self.p.days(w.region, rs[0]))
            region, slots = open_slots[0]
            if w.region == region:
                continue
            tb = self.s.building_at(region)
            cost = 0 if (tb and tb.side is self.m.me) else MOVE_COST
            cands.append(Cand(P_WORKFILL, "move", wid=w.wid, region=region, cost=cost))
            claimed_w.add(w.wid)
            if slots <= 1:
                open_slots.pop(0)
            else:
                open_slots[0][1] -= 1
        # remaining idle warriors not on a friendly building -> nearest building
        for w in self.free_warriors(claimed_w):
            b_here = self.s.building_at(w.region)
            if b_here is not None and b_here.side is self.m.me:
                continue
            bs = self.my_buildings()
            if not bs:
                continue
            target = min(bs, key=lambda b: self.p.days(w.region, b.region)).region
            if target == w.region:
                continue
            tb = self.s.building_at(target)
            cost = 0 if (tb and tb.side is self.m.me) else MOVE_COST
            cands.append(Cand(P_IDLE, "move", wid=w.wid, region=target, cost=cost))
            claimed_w.add(w.wid)

    def desired_warriors_for_now(self, phase: str, behind_econ: bool) -> int:
        worker_slots = sum(b.work_cap for b in self.my_buildings())
        base_count = len(self.my_bases())

        if base_count <= 2:
            defense_reserve = 1
        elif base_count <= 4:
            defense_reserve = 2
        else:
            defense_reserve = 3

        expansion_reserve = 0
        if self.central_candidate_sites() and self.can_start_new_base_move(MOVE_COST):
            expansion_reserve = 1

        attack_reserve = 2 if behind_econ else 0
        desired = worker_slots + defense_reserve + expansion_reserve + attack_reserve

        if base_count == 2:
            desired = min(max(desired, self.MIN_EXPAND_WARRIORS),
                          self.POST_SECOND_BASE_TRAIN_CAP)
        if phase == "MID":
            desired = min(desired, self.MID_PASSIVE_TRAIN_CAP)
        if phase == "LATE":
            desired = min(max(desired, 6), self.LATE_PASSIVE_TRAIN_CAP)
        return desired

    def should_train_after_second_base(self, phase, behind_econ) -> bool:
        return len(self.my_warriors()) < self.desired_warriors_for_now(phase, behind_econ)

    def _plan_growth_training(self, cands, phase, behind_econ):
        hq = self.s.building_at(self.m.my_hq)
        if hq is None or self.enemy_at(self.m.my_hq):
            return
        if self.pending_unbuilt_zones():
            return
        if len(self.my_bases()) < 2:
            return

        desired = self.desired_warriors_for_now(phase, behind_econ)
        if len(self.my_warriors()) >= desired:
            return

        if not self.can_spend_noncritical(self.s.gold, TRAIN_COST, extra_train=1):
            return
        n = len(self.my_warriors())
        net = self.income(self.m.me) - UPKEEP_PER_WARRIOR * n
        if net < UPKEEP_PER_WARRIOR:
            return

        if len(self.my_warriors()) >= self.MIN_EXPAND_WARRIORS:
            next_base_fund = (BASE_BUILD_COST + MOVE_COST
                              + self.food_reserve(extra_train=1)
                              + self.NEW_BASE_BUFFER)
            if self.central_candidate_sites() and self.s.gold - TRAIN_COST < next_base_fund:
                return

        cands.append(Cand(P_TRAIN_GROW, "train", cost=TRAIN_COST))

    def is_empty_site_move(self, c: Cand) -> bool:
        if c.kind != "move" or c.region is None:
            return False
        if c.region not in self.m.site_set:
            return False
        return self.s.building_at(c.region) is None

    # -- final selection: gold budget + food guard ----------------------------
    def _select(self, cands: list[Cand]) -> Actions:
        cands.sort(key=lambda c: (-c.prio, c.cost))
        gold = self.s.gold
        used_w: set[str] = set()
        used_r: set[int] = set()
        hq = self.s.building_at(self.m.my_hq)
        train_cap = hq.train_cap if hq else 0
        train_n = 0
        new_base_move_selected = False
        chosen: list[Cand] = []

        for c in cands:
            extra_train = 1 if c.kind == "train" else 0
            expected_train = train_n + extra_train
            if c.cost > gold:
                continue
            if c.prio < CRITICAL_PRIO:
                if gold - c.cost < self.food_reserve(extra_train=expected_train):
                    continue
            empty_site_move = self.is_empty_site_move(c)
            if empty_site_move and c.prio < CRITICAL_PRIO:
                if new_base_move_selected:
                    continue
                if self.pending_unbuilt_zones():
                    continue
                if not self.can_start_new_base_move(c.cost, extra_train=expected_train,
                                                    current_gold=gold):
                    continue
            if c.kind == "move":
                if c.wid in used_w:
                    continue
                w = self.s.warriors.get(c.wid)
                if w is None or w.moving or not self.p.reachable(w.region, c.region):
                    continue
                used_w.add(c.wid)
            elif c.kind == "upgrade":
                if c.region in used_r or not self._upgrade_valid(c.region):
                    continue
                used_r.add(c.region)
            elif c.kind == "train":
                if train_n >= train_cap:
                    continue
                train_n += 1
            else:
                continue
            if c.kind == "move" and empty_site_move and c.prio < CRITICAL_PRIO:
                new_base_move_selected = True
            gold -= c.cost
            chosen.append(c)

        chosen = self._food_guard(chosen)
        return self._build_actions(chosen)

    @staticmethod
    def _build_actions(chosen: list[Cand]) -> Actions:
        a = Actions()
        for c in chosen:
            if c.kind == "move":
                a.moves.append((c.wid, c.region))
            elif c.kind == "upgrade":
                a.upgrades.append(c.region)
            elif c.kind == "train":
                a.train_n += 1
        return a

    def _upgrade_valid(self, region: int) -> bool:
        if region not in self.m.site_set and region not in (0, self.m.N - 1):
            return False
        if self.enemy_at(region) or not self.warriors_at(region, self.m.me):
            return False
        b = self.s.building_at(region)
        if b is None:
            return region in self.m.site_set         # build base on an empty site
        return b.side is self.m.me

    def _estimated_income(self, moves: list[tuple[str, int]]) -> int:
        """Income this evening after commanded and already-running movement."""
        pos = {w.wid: w.region for w in self.my_warriors()}
        commanded = dict(moves)
        for w in self.my_warriors():
            target = commanded.get(w.wid)
            if target is None and w.moving:
                target = w.target
            if target is None or target == w.region:
                continue
            if self.enemy_at(w.region):
                continue
            pos[w.wid] = self.p.next_step(w.region, target)
        total = 0
        for b in self.my_buildings():
            cnt = sum(1 for w in self.my_warriors() if pos.get(w.wid) == b.region)
            total += WORK_INCOME * min(cnt, b.work_cap)
        return total

    def _food_guard(self, chosen: list[Cand]) -> list[Cand]:
        """Drop lowest-priority, non-critical commands if evening supply would
        put gold below zero. Critical defence is kept."""
        n_after = len(self.my_warriors()) + sum(1 for c in chosen if c.kind == "train")

        def projected(cs):
            moves = [(c.wid, c.region) for c in cs if c.kind == "move"]
            income = self._estimated_income(moves)
            spent = sum(c.cost for c in cs)
            food = UPKEEP_PER_WARRIOR * n_after
            return self.s.gold - spent + income - food

        if projected(chosen) >= 0:
            return chosen
        keep = list(chosen)
        trimmable = sorted([c for c in keep if c.prio < CRITICAL_PRIO],
                           key=lambda c: (c.prio, -c.cost))
        for c in trimmable:
            if projected(keep) >= 0:
                break
            keep.remove(c)
            if c.kind == "train":
                n_after -= 1
        return keep


# ───────────────────────────── main loop ──────────────────────────────────────
def main() -> None:
    m, s = parse_init()
    p = Paths(m)                # precompute during the 1s handshake window
    brain = Brain(m, s, p)
    print("OK", flush=True)

    while (turn := read_turn_start()) is not None:
        try:
            actions = brain.decide(turn)
        except Exception as e:  # never crash mid-game (-> WA); fall back to idle
            dbg("decide error:", repr(e))
            actions = Actions()
        emit(actions)
        read_turn_result(s, m, actions)


if __name__ == "__main__":
    main()

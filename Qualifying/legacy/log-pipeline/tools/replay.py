#!/usr/bin/env python3
"""NEXT NATION 로그 리플레이 -> 정확 수치 facts sheet.

이 도구는 referee 풀 로그(<id>.txt)를 파싱하여, 로그에 **직접 기록되지 않은
골드만 규칙으로 정확히 역산**하고, 나머지(병력 수/유닛 위치/전투/공성/건물)는 로그
이벤트를 그대로 재생하여 사람이 읽는 분석의 근거가 되는 facts sheet를 만든다.

핵심 원리 (why this is exact, not an estimate):
  라이브 플레이에선 상대의 이동 지출을 몰라 골드를 추정했지만, 완성된 로그에는
  양측의 모든 명령(COMMAND 블록)과 매 턴 유닛 위치(MOVE 결과)·전투·공성·HUNGER가
  전부 찍혀 있다. 따라서 수입/지출/업킵을 규칙 공식대로 턴별 역산하면 골드가 확정된다.

자체 검증(HUNGER gate):
  "내 골드로 이번 저녁 업킵을 감당 못 한다"고 계산된 전사 수 ==
  로그의 DAMAGE HUNGER 이벤트 수. 불일치하면 모델 오차이므로 경고를 남긴다.

규칙 상수 출처: 프로젝트 루트 Rule.md §1~4 (본부/기지 레벨표 포함).
  주의: Seongho/next_nation_analyzer/core/rules_config.py 의 상수는 플레이스홀더라
  실제 규칙과 다르다(그 값들을 재사용하지 말 것). 여기 값이 Rule.md 기준 정본이다.

사용법:
  python replay.py <log.txt> [--out facts.md] [--json facts.json]
  --out 미지정 시 facts markdown 을 stdout 으로 출력.

의존성: Python 표준 라이브러리만 사용 (venv 불필요).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from collections import defaultdict

# ---------------------------------------------------------------------------
# 규칙 상수 (Rule.md 기준 정본)
# ---------------------------------------------------------------------------
INITIAL_GOLD = 500
INITIAL_WARRIORS = 3          # A1..A3 / B1..B3, 시작 HP 4
INITIAL_UNIT_HP = 4
MOVE_COST = 10                # 신규 이동 명령당 (목적지가 아군 건물이면 0)
TRAIN_COST = 120              # 훈련 1명당
UPKEEP = 2                    # 전사당 매 저녁 식비
WORK_UNIT_INCOME = 15         # 건물마다 15 * min(구역 아군 전사 수, 노동 가능 수)
MAX_TURN = 200

# 본부(HQ): level -> 속성.  upgrade_cost = 이 레벨에 "도달"하는 비용.
HQ = {
    1: {"upgrade_cost": 0,    "max_hp": 10, "turret": 1, "train_cap": 1, "work_cap": 1, "unit_hp": 4},
    2: {"upgrade_cost": 600,  "max_hp": 15, "turret": 2, "train_cap": 1, "work_cap": 2, "unit_hp": 5},
    3: {"upgrade_cost": 1200, "max_hp": 20, "turret": 2, "train_cap": 2, "work_cap": 3, "unit_hp": 6},
    4: {"upgrade_cost": 2400, "max_hp": 25, "turret": 3, "train_cap": 2, "work_cap": 4, "unit_hp": 7},
    5: {"upgrade_cost": 3600, "max_hp": 30, "turret": 3, "train_cap": 3, "work_cap": 5, "unit_hp": 8},
}
HQ_MAX_LEVEL = 5
HQ_REPAIR_COST = 1000         # L5 수리

# 기지(BASE): level -> 속성.  build_cost = 이 레벨에 "도달"하는 비용(L1=신규 건설).
BASE = {
    1: {"build_cost": 300,  "max_hp": 6,  "turret": 1, "work_cap": 1},
    2: {"build_cost": 600,  "max_hp": 12, "turret": 1, "work_cap": 2},
    3: {"build_cost": 1000, "max_hp": 18, "turret": 2, "work_cap": 3},
}
BASE_MAX_LEVEL = 3
BASE_REPAIR_COST = 500        # L3 수리

SIDE_MAP = {"LEFT": "A", "RIGHT": "B", "A": "A", "B": "B"}


def unit_side(uid):
    if uid and uid[0] == "A":
        return "A"
    if uid and uid[0] == "B":
        return "B"
    return None


# ---------------------------------------------------------------------------
# 파싱
# ---------------------------------------------------------------------------
class ParseResult:
    def __init__(self):
        self.n = 0
        self.k = 0
        self.strongholds = []
        self.turns = []          # list of dict(turn_no, commands{A:[],B:[]}, events[])
        self.result = {"outcome": "", "reason": "", "raw": ""}
        self.warnings = []       # list of (line_no, code, raw)


def parse_log(text):
    pr = ParseResult()
    lines = text.splitlines()
    i, ntot = 0, len(lines)
    while i < ntot:
        line = lines[i].strip()
        if not line:
            i += 1
            continue
        if line.startswith("[LEFT") or line.startswith("[RIGHT"):
            i += 1
        elif line == "MAP":
            i = _parse_map(lines, i + 1, pr)
        elif line.startswith("TURN ") and not line.endswith(" RESULT"):
            i = _parse_turn(lines, i, pr)
        elif line.startswith("RESULT "):
            parts = line.split(None, 2)
            pr.result = {
                "outcome": parts[1] if len(parts) > 1 else "",
                "reason": parts[2] if len(parts) > 2 else "",
                "raw": line,
            }
            i += 1
        else:
            pr.warnings.append((i + 1, "unknown_top_level_line", line))
            i += 1
    return pr


def _parse_map(lines, i, pr):
    try:
        pr.n, pr.k = [int(v) for v in lines[i].split()]
        # lines[i+1] = x coords, lines[i+2] = y coords (사용 안 함, 넘어감)
        strong = lines[i + 3].split()
        pr.strongholds = [int(v) for v in strong[1:]]  # drop leading "STRONGHOLDS"
        i += 4
        for _ in range(pr.n):
            i += 1  # adjacency line (미사용)
        if i < len(lines) and lines[i].strip() == "END MAP":
            i += 1
        else:
            pr.warnings.append((i + 1, "missing_end_map", ""))
        return i
    except Exception as exc:  # noqa: BLE001
        pr.warnings.append((i + 1, "map_parse_error", str(exc)))
        while i < len(lines) and lines[i].strip() != "END MAP":
            i += 1
        return min(i + 1, len(lines))


def _parse_turn(lines, i, pr):
    raw = lines[i].strip()
    try:
        turn_no = int(raw.split()[1])
    except Exception:  # noqa: BLE001
        turn_no = -1
        pr.warnings.append((i + 1, "bad_turn_header", raw))
    turn = {"turn_no": turn_no, "commands": {"A": [], "B": []}, "events": []}
    i += 1
    ntot = len(lines)
    while i < ntot:
        line = lines[i].strip()
        if line.startswith("COMMAND ") and line.endswith(" START"):
            side_name = line.split()[1]
            side = SIDE_MAP.get(side_name, side_name)
            i += 1
            end_tok = "COMMAND %s END" % side_name
            while i < ntot and lines[i].strip() != end_tok:
                cl = lines[i].strip()
                if cl:
                    cmd = _parse_command(cl)
                    if cmd:
                        turn["commands"].setdefault(side, []).append(cmd)
                    else:
                        pr.warnings.append((i + 1, "unknown_command", cl))
                i += 1
            i += 1
        elif line == "TURN %d RESULT" % turn_no or line == ("TURN %s RESULT" % turn_no):
            i += 1
            end_tok = "END TURN %d" % turn_no
            while i < ntot and lines[i].strip() != end_tok:
                el = lines[i].strip()
                if el:
                    evs = _parse_event(el)
                    if evs is None:                 # 진짜 미인식 라인만 경고
                        pr.warnings.append((i + 1, "unknown_event", el))
                    else:
                        turn["events"].extend(evs)   # []도 정상(예: TIME)
                i += 1
            if i < ntot:
                i += 1
            pr.turns.append(turn)
            return i
        elif line.startswith("END TURN "):
            i += 1
            pr.turns.append(turn)
            return i
        else:
            pr.warnings.append((i + 1, "unexpected_turn_line", line))
            i += 1
    pr.turns.append(turn)
    return i


def _parse_command(raw):
    p = raw.split()
    if not p:
        return None
    if p[0] == "MOVE" and len(p) == 3:
        return {"type": "MOVE", "actor": p[1], "value": int(p[2])}
    if p[0] == "TRAIN" and len(p) == 2:
        return {"type": "TRAIN", "actor": None, "value": int(p[1])}
    if p[0] == "UPGRADE" and len(p) == 2:
        return {"type": "UPGRADE", "actor": None, "value": int(p[1])}
    return None


def _ev(typ, side=None, uid=None, loc=None, amount=None, cause=None):
    return {"type": typ, "side": side, "uid": uid, "loc": loc, "amount": amount, "cause": cause}


def _parse_event(raw):
    """인식한 라인 -> Event 리스트([] 가능). 완전히 미인식 -> None (경고 대상)."""
    p = raw.split()
    if not p:
        return []
    t = p[0]
    if t == "TIME":
        return []  # 인식됨(골드/상태에 무관)
    if t == "UPGRADE" and len(p) == 3:
        return [_ev("UPGRADE", side=SIDE_MAP.get(p[1]), loc=int(p[2]))]
    if t == "TRAIN" and len(p) >= 2:
        return [_ev("TRAIN", side=unit_side(u), uid=u) for u in p[1:] if unit_side(u)]
    if t == "MOVE" and len(p) == 3:
        return [_ev("MOVE", side=unit_side(p[1]), uid=p[1], loc=int(p[2]))]
    if t == "DAMAGE" and len(p) == 4:
        return [_ev("DAMAGE", side=unit_side(p[2]), uid=p[2], amount=int(p[3]), cause=p[1])]
    if t == "SIEGE" and len(p) == 4:
        return [_ev("SIEGE", side=SIDE_MAP.get(p[1]), loc=int(p[2]), amount=int(p[3]))]
    return None


# ---------------------------------------------------------------------------
# 리플레이 (상태 재생 + 골드 역산)
# ---------------------------------------------------------------------------
class Unit:
    __slots__ = ("uid", "side", "spawn_turn", "spawn_region", "loc", "hp", "max_hp",
                 "alive", "death_turn", "death_cause", "path")

    def __init__(self, uid, side, spawn_turn, region, max_hp):
        self.uid = uid
        self.side = side
        self.spawn_turn = spawn_turn
        self.spawn_region = region
        self.loc = region
        self.hp = max_hp
        self.max_hp = max_hp
        self.alive = True
        self.death_turn = None
        self.death_cause = None
        self.path = [(spawn_turn, region)]   # (turn, region) at each move


class Building:
    __slots__ = ("side", "region", "is_hq", "level", "hp", "max_hp", "built_turn", "destroyed_turn")

    def __init__(self, side, region, is_hq, level, hp, built_turn):
        self.side = side
        self.region = region
        self.is_hq = is_hq
        self.level = level
        self.hp = hp
        self.max_hp = hp
        self.built_turn = built_turn
        self.destroyed_turn = None


def replay(pr):
    n = pr.n
    hq_region = {"A": 0, "B": (n - 1 if n else -1)}
    units = {"A": {}, "B": {}}
    buildings = {"A": {}, "B": {}}
    gold = {"A": INITIAL_GOLD, "B": INITIAL_GOLD}
    # 진행 중인 이동 명령 {side: {uid: 최종목적지}}.
    # Rule: 이동명령은 도착 전까지 재명령 불가 -> 신규(대기 중) 유닛에만 이동비 과금.
    moving = {"A": {}, "B": {}}
    gold_underflow = []          # (turn, side, need, have): 로그 지출>역산 골드 -> 실제 골드가 더 많았다는 증거

    # 초기 상태: 본부 L1 + 전사 3명
    for s in ("A", "B"):
        buildings[s][hq_region[s]] = Building(s, hq_region[s], True, 1, HQ[1]["max_hp"], 0)
        for j in range(1, INITIAL_WARRIORS + 1):
            uid = "%s%d" % (s, j)
            units[s][uid] = Unit(uid, s, 0, hq_region[s], INITIAL_UNIT_HP)

    per_turn = []            # 턴별 경제/상태 스냅샷
    timings = {"A": {}, "B": {}}   # first_train/move/upgrade/base/combat/siege
    building_events = []     # (turn, side, region, action, level, cost)
    combat_log = []          # (turn, side, uid, cause, region, amount)
    siege_log = []           # (turn, side, region, amount)  side = 피해 입은 건물 소속
    hq_pressure = []         # (turn, defender_side, [enemy uids in hq region], siege_dmg)
    hunger_flags = []        # (turn, side, predicted, actual)
    dispatches = []          # (turn, side, uid, dest, own_bld) — 신규 이동명령 발령 기록(파병 신호용)

    def set_first(side, key, turn):
        if key not in timings[side]:
            timings[side][key] = turn

    for turn in pr.turns:
        t = turn["turn_no"]
        spend = {"A": {"move": 0, "train": 0, "upgrade": 0}, "B": {"move": 0, "train": 0, "upgrade": 0}}

        # --- 이번 턴 UPGRADE(건설) 대상 미리 파악 (같은 턴 건설 후 이동=무료 처리를 위해) ---
        built_this_turn = {"A": set(), "B": set()}
        for e in turn["events"]:
            if e["type"] == "UPGRADE" and e["side"]:
                built_this_turn[e["side"]].add(e["loc"])

        # === 아침 1) 이동비: COMMAND 블록의 신규 MOVE 명령 (목적지가 아군 건물이면 무료) ===
        # 이미 이동 중(active order)인 유닛의 MOVE 재등장은 규칙상 무시(재과금 없음).
        new_moves = {"A": 0, "B": 0}    # 이번 턴 신규 이동 명령 수(무료 포함) — 대량이동 신호용
        rally_home = {"A": 0, "B": 0}   # 목적지가 우리 본부 구역인 신규 이동(회군/방어 rally 신호용)
        for side in ("A", "B"):
            for c in turn["commands"].get(side, []):
                if c["type"] != "MOVE":
                    continue
                uid, dest = c["actor"], c["value"]
                if uid in moving[side]:
                    continue  # 도착 전 재명령 -> 무시
                own_bld = dest in buildings[side] or dest in built_this_turn[side]
                if not own_bld:
                    spend[side]["move"] += MOVE_COST
                moving[side][uid] = dest
                dispatches.append((t, side, uid, dest, bool(own_bld)))
                new_moves[side] += 1
                if dest == hq_region[side]:
                    rally_home[side] += 1
                set_first(side, "first_move", t)

        # === 아침 2) 건설/업그레이드/수리: RESULT UPGRADE 이벤트 ===
        for e in turn["events"]:
            if e["type"] != "UPGRADE" or not e["side"]:
                continue
            side, r = e["side"], e["loc"]
            is_hq = (r == hq_region[side])
            b = buildings[side].get(r)
            if is_hq:
                b = buildings[side][r]  # 항상 존재
                if b.level < HQ_MAX_LEVEL:
                    b.level += 1
                    cost = HQ[b.level]["upgrade_cost"]
                    b.max_hp = HQ[b.level]["max_hp"]
                    b.hp = b.max_hp
                    action = "upgrade"
                else:
                    cost = HQ_REPAIR_COST
                    b.hp = b.max_hp
                    action = "repair"
            else:
                if b is None:
                    b = Building(side, r, False, 1, BASE[1]["max_hp"], t)
                    buildings[side][r] = b
                    cost = BASE[1]["build_cost"]
                    action = "build"
                    set_first(side, "first_base", t)
                elif b.level < BASE_MAX_LEVEL:
                    b.level += 1
                    cost = BASE[b.level]["build_cost"]
                    b.max_hp = BASE[b.level]["max_hp"]
                    b.hp = b.max_hp
                    action = "upgrade"
                else:
                    cost = BASE_REPAIR_COST
                    b.hp = b.max_hp
                    action = "repair"
            spend[side]["upgrade"] += cost
            set_first(side, "first_upgrade", t)
            building_events.append((t, side, r, action, b.level, cost))

        # === 아침 3) 훈련: RESULT TRAIN 이벤트 (본부 레벨에 따른 HP로 스폰) ===
        for e in turn["events"]:
            if e["type"] != "TRAIN" or not e["side"]:
                continue
            side = e["side"]
            hqb = buildings[side][hq_region[side]]
            uhp = HQ[hqb.level]["unit_hp"]
            u = Unit(e["uid"], side, t, hq_region[side], uhp)
            units[side][e["uid"]] = u
            spend[side]["train"] += TRAIN_COST
            set_first(side, "first_train", t)

        # === 아침 이동 반영: RESULT MOVE 이벤트 (위치/경로 갱신) ===
        for e in turn["events"]:
            if e["type"] != "MOVE" or not e["side"]:
                continue
            u = units[e["side"]].get(e["uid"])
            if u is None:  # 로그에 훈련 없이 등장(초기 유닛 등) — 방어적 생성
                u = Unit(e["uid"], e["side"], t, e["loc"], INITIAL_UNIT_HP)
                units[e["side"]][e["uid"]] = u
            if u.loc != e["loc"]:
                u.path.append((t, e["loc"]))
            u.loc = e["loc"]

        # 도착한 이동 명령 정리 (loc == 목적지 -> 명령 종료, 재과금 가능 상태로)
        for side in ("A", "B"):
            for uid, tgt in list(moving[side].items()):
                u = units[side].get(uid)
                if u is None or u.loc == tgt:
                    moving[side].pop(uid, None)

        # === 낮) 전투/포탑 피해 + 공성 (HUNGER 제외) ===
        for e in turn["events"]:
            if e["type"] == "DAMAGE" and e["cause"] in ("COMBAT", "TURRET") and e["side"]:
                u = units[e["side"]].get(e["uid"])
                combat_log.append((t, e["side"], e["uid"], e["cause"],
                                   u.loc if u else None, e["amount"]))
                set_first(e["side"], "first_combat", t)
                if u and u.alive:
                    u.hp -= e["amount"]
                    if u.hp <= 0:
                        u.alive = False
                        u.death_turn = t
                        u.death_cause = e["cause"]
                        moving[e["side"]].pop(e["uid"], None)
            elif e["type"] == "SIEGE" and e["side"]:
                side, r = e["side"], e["loc"]
                siege_log.append((t, side, r, e["amount"]))
                set_first(side, "first_siege_against", t)
                b = buildings[side].get(r)
                if b and b.destroyed_turn is None:
                    b.hp -= e["amount"]
                    if b.hp <= 0:
                        b.destroyed_turn = t

        # === 저녁 1) 노동 수입: 건물마다 15 * min(구역 아군 전사 수, 노동 가능 수) ===
        # 위치별 생존 전사 수(낮 전투 후)
        region_count = {"A": {}, "B": {}}
        for side in ("A", "B"):
            for u in units[side].values():
                if u.alive:
                    region_count[side][u.loc] = region_count[side].get(u.loc, 0) + 1
        income = {"A": 0, "B": 0}
        for side in ("A", "B"):
            for r, b in buildings[side].items():
                if b.destroyed_turn is not None:
                    continue
                cap = HQ[b.level]["work_cap"] if b.is_hq else BASE[b.level]["work_cap"]
                workers = region_count[side].get(r, 0)
                income[side] += WORK_UNIT_INCOME * min(workers, cap)

        # === 골드 흐름: 시작 - 아침지출 + 수입 => 업킵 직전 ===
        alive_pre_upkeep = {s: sum(1 for u in units[s].values() if u.alive) for s in ("A", "B")}
        upkeep = {"A": 0, "B": 0}
        for side in ("A", "B"):
            morning_spend = spend[side]["move"] + spend[side]["train"] + spend[side]["upgrade"]
            # 언더플로: 로그가 보여주는 아침 지출을 역산 골드로 감당 못 함
            #   -> referee가 허용했다는 것은 실제 골드가 더 많았다는 확정 증거(수입 과소 등).
            if gold[side] < morning_spend:
                gold_underflow.append((t, side, morning_spend, gold[side]))
            gold[side] = gold[side] - morning_spend + income[side]
            before_upkeep = gold[side]
            can_pay = before_upkeep // UPKEEP if before_upkeep > 0 else 0
            paid_units = min(alive_pre_upkeep[side], can_pay)
            predicted_hunger = max(0, alive_pre_upkeep[side] - paid_units)
            upkeep[side] = paid_units * UPKEEP
            gold[side] = before_upkeep - upkeep[side]
            # 실제 HUNGER 이벤트 수와 대조
            actual_hunger = sum(1 for e in turn["events"]
                                if e["type"] == "DAMAGE" and e["cause"] == "HUNGER" and e["side"] == side)
            if predicted_hunger != actual_hunger:
                # hard=로그엔 기아 있는데 예측 못함(골드 과대추정=진짜 오류) / drift=예측만 있고 로그엔 없음(골드 과소=하한)
                kind = "hard" if actual_hunger > predicted_hunger else "drift"
                hunger_flags.append((t, side, predicted_hunger, actual_hunger, kind))

        # === 저녁 2) HUNGER 피해 반영 (식비 부족으로 HP 손실 -> 0이면 퇴각) ===
        for e in turn["events"]:
            if e["type"] == "DAMAGE" and e["cause"] == "HUNGER" and e["side"]:
                u = units[e["side"]].get(e["uid"])
                if u and u.alive:
                    u.hp -= e["amount"]
                    if u.hp <= 0:
                        u.alive = False
                        u.death_turn = t
                        u.death_cause = "HUNGER"

        # === 본진 압박: 방어측 본부 구역에 있는 적 유닛 ===
        for defender in ("A", "B"):
            attacker = "B" if defender == "A" else "A"
            hr = hq_region[defender]
            enemies = [u.uid for u in units[attacker].values() if u.alive and u.loc == hr]
            sd = sum(a for (tt, sside, rr, a) in siege_log if tt == t and sside == defender and rr == hr)
            if enemies or sd:
                hq_pressure.append((t, defender, enemies, sd))

        # === 턴 스냅샷 ===
        snap = {"turn": t}
        for side in ("A", "B"):
            alive = sum(1 for u in units[side].values() if u.alive)
            bld = [b for b in buildings[side].values() if b.destroyed_turn is None]
            snap[side] = {
                "gold": gold[side],
                "alive": alive,
                "buildings": len(bld),
                "levels": sorted((b.level for b in bld), reverse=True),
                "income": income[side],
                "spend_move": spend[side]["move"],
                "spend_train": spend[side]["train"],
                "spend_upgrade": spend[side]["upgrade"],
                "upkeep": upkeep[side],
                "hq_hp": buildings[side][hq_region[side]].hp,
                "hq_level": buildings[side][hq_region[side]].level,
                "new_moves": new_moves[side],
                "rally_home": rally_home[side],
                "trained": spend[side]["train"] // TRAIN_COST,
            }
        per_turn.append(snap)

    return {
        "n": n, "k": pr.k, "hq_region": hq_region, "strongholds": pr.strongholds,
        "result": pr.result, "warnings": pr.warnings,
        "units": units, "buildings": buildings,
        "per_turn": per_turn, "timings": timings,
        "building_events": building_events, "combat_log": combat_log,
        "siege_log": siege_log, "hq_pressure": hq_pressure,
        "hunger_flags": hunger_flags, "gold_underflow": gold_underflow,
        "dispatches": dispatches,
    }


# ---------------------------------------------------------------------------
# facts sheet 렌더링
# ---------------------------------------------------------------------------
def _compress_path(path):
    """[(turn,region),...] -> "r0@t0 -> r1@t1 -> ..." (연속 중복 region 접기)."""
    out = []
    for (t, r) in path:
        if not out or out[-1][1] != r:
            out.append((t, r))
    return " -> ".join("%d@t%d" % (r, t) for (t, r) in out)


def _build_occupancy(facts, end_turn):
    """유닛 .path 를 spawn~(사망 or end)까지 forward-fill 해 턴별 구역 점령 재구성.
    반환 occ[t] = {"A": {region: [uids]}, "B": {region: [uids]}} (생존 구간만 포함).
    적 수비 병력 카운트(과소 파병·거점 레이스 판정)에 쓰는 결정론적 관찰치."""
    occ = {}
    for side in ("A", "B"):
        for uid, u in facts["units"][side].items():
            path = u.path
            if not path:
                continue
            last_t = u.death_turn if u.death_turn is not None else end_turn
            pi = 0
            for t in range(u.spawn_turn, last_t + 1):
                while pi + 1 < len(path) and path[pi + 1][0] <= t:
                    pi += 1
                r = path[pi][1]
                occ.setdefault(t, {"A": {}, "B": {}})[side].setdefault(r, []).append(uid)
    return occ


def _garrison(occ, side, region, t):
    d = occ.get(t)
    return len(d[side].get(region, [])) if d else 0


def _level_at(facts, side, region, t, is_hq=False):
    """building_events 로 t 시점 레벨·누적 투자골드 재구성.
    Building 객체의 level 은 이후 재건/업글로 오염될 수 있어(파괴 후 같은 구역 재건이
    upgrade 이벤트로 기록되는 리플레이 특성) 이벤트 기준으로 재구성한다.
    재건 시(마지막 build 이후)의 incarnation 만 집계. HQ는 L1에서 시작."""
    lvl, invested = (1, 0) if is_hq else (0, 0)
    for (tt, s, r, action, _lvl, cost) in facts["building_events"]:
        if s != side or r != region or tt > t:
            continue
        if action == "build":
            lvl, invested = 1, cost
        elif action == "upgrade":
            lvl += 1
            invested += cost
        # repair: 레벨 불변
    return lvl, invested


def _uid_key(uid):
    return (uid[0], int(uid[1:]) if uid[1:].isdigit() else 0)


def _sign(x):
    return (x > 0) - (x < 0)


def _assault_summary(facts, defender, attacker, win, tmap, k, hqreg):
    """압박 윈도우 -> 본진 공격 시도 요약(성공=적HQ 파괴, 실패=투입/전사)."""
    peak, uids = 0, set()
    for t in win:
        u, _sd = tmap[t]
        peak = max(peak, len(u))
        uids |= u
    if peak < k:
        return None
    start, end = win[0], win[-1]
    dt = facts["buildings"][defender][hqreg[defender]].destroyed_turn
    success = dt is not None and start - 1 <= dt <= end + 2
    losses = 0
    for uid in uids:
        uu = facts["units"][attacker].get(uid)
        if uu and uu.death_turn is not None and start <= uu.death_turn <= end + 3:
            losses += 1
    return {"attacker": attacker, "defender": defender, "start": start, "end": end,
            "peak": peak, "units": sorted(uids, key=_uid_key), "success": success, "losses": losses}


def derive_signals(facts):
    """중립 관찰 신호 산출(승패조건·성장격차·오더품질). '손해 오더' 판정은 LLM이 POLICY로 수행."""
    pt = facts["per_turn"]
    hqreg = facts["hq_region"]
    res = facts["result"]
    reason, outcome = res.get("reason", ""), res.get("outcome", "")
    other = {"A": "B", "B": "A"}
    winner = {"LEFT_WIN": "A", "RIGHT_WIN": "B"}.get(outcome)

    sig = {"winner": winner,
           "cond_type": ("combat_finish" if reason == "HQ_DESTROYED"
                         else "hq_hp" if reason == "TURN_LIMIT"
                         else (reason or "unknown"))}

    # 마무리 공격조 (HQ_DESTROYED)
    sig["finisher"] = None
    if reason == "HQ_DESTROYED":
        losers = [s for s in ("A", "B")
                  if facts["buildings"][s][hqreg[s]].destroyed_turn is not None]
        loser = losers[0] if losers else (other.get(winner) if winner else None)
        if loser is not None:
            dt = facts["buildings"][loser][hqreg[loser]].destroyed_turn
            atk = set()
            for (tt, defender, enemies, _sd) in facts["hq_pressure"]:
                if defender == loser and dt - 6 <= tt <= dt:
                    atk.update(enemies)
            sig["finisher"] = {"loser": loser, "attacker": other[loser], "turn": dt,
                               "units": sorted(atk, key=_uid_key)}

    # 성장 격차 (건물수 / 누적수입) 정착 턴 + 사분위 스냅샷
    cum = {"A": 0, "B": 0}
    bgap, igap = [], []
    for s in pt:
        for side in ("A", "B"):
            cum[side] += s[side]["income"]
        bgap.append((s["turn"], s["A"]["buildings"] - s["B"]["buildings"]))
        igap.append((s["turn"], cum["A"] - cum["B"]))

    def settle(series):
        if not series or _sign(series[-1][1]) == 0:
            return None
        fs, st = _sign(series[-1][1]), series[-1][0]
        for (t, g) in reversed(series):
            if _sign(g) == fs:
                st = t
            else:
                break
        return st

    quart = []
    for q in (0.25, 0.5, 0.75, 1.0):
        if pt:
            s = pt[min(len(pt) - 1, max(0, int(round(q * len(pt))) - 1))]
            quart.append({"turn": s["turn"],
                          "build": (s["A"]["buildings"], s["B"]["buildings"]),
                          "alive": (s["A"]["alive"], s["B"]["alive"]),
                          "hq_level": (s["A"]["hq_level"], s["B"]["hq_level"]),
                          "hq_hp": (s["A"]["hq_hp"], s["B"]["hq_hp"])})
    sig["growth"] = {"build_gap_final": bgap[-1][1] if bgap else 0,
                     "build_settle": settle(bgap),
                     "cumincome_gap_final": igap[-1][1] if igap else 0,
                     "cumincome_settle": settle(igap), "quartiles": quart}

    # 오더 품질 신호
    MOVE_BURST_K, ASSAULT_K, RALLY_K, TRAIN_BURST_K = 5, 3, 3, 3
    train_burst, move_burst, income_drop, rally = [], [], [], []
    for i, s in enumerate(pt):
        for side in ("A", "B"):
            d = s[side]
            if d["trained"] >= 2:
                train_burst.append((s["turn"], side, d["trained"]))
            if d["new_moves"] >= MOVE_BURST_K:
                move_burst.append((s["turn"], side, d["new_moves"]))
            if d.get("rally_home", 0) >= RALLY_K:
                rally.append((s["turn"], side, d["rally_home"]))
            base = max((pt[j][side]["income"] for j in range(max(0, i - 4), i)), default=d["income"])
            if base - d["income"] >= 30 and d["income"] < base * 0.6:
                income_drop.append((s["turn"], side, base, d["income"]))

    def threat_near(side, t, back=3, fwd=8):
        # 비대칭 창: 과거 back턴 ~ 미래 fwd턴. 미래를 넓게 봐서 '접근 중이던 웨이브가
        # 실제로 본부에 도달했으면' 방어 rally는 정당(헛발동 아님)으로 처리 -> 오검출 억제.
        for (tt, defender, enemies, sd) in facts["hq_pressure"]:
            if defender == side and -back <= (tt - t) <= fwd and (enemies or sd):
                return True
        return False

    # 헛발동 후보(보수적, 단정): 위협(이후 8턴 내 본부권 전투/공성 포함) 없는데 훈련 폭증(≥3).
    #   본부 rally(dest=본부구역)는 방어 회군과 HQ 채굴 복귀가 구분 불가 -> '헛발동' 단정 금지,
    #   중립 관찰(rally)로만 보고하고 궤적으로 LLM이 회군/채굴 판정. 수입 급락도 중립 관찰.
    futile_train = [x for x in train_burst if x[2] >= TRAIN_BURST_K and not threat_near(x[1], x[0])]
    rally_no_threat = [x for x in rally if not threat_near(x[1], x[0])]   # 중립: 회군 or 채굴 복귀

    # 본진 공격 시도·결과
    press = {}
    for (tt, defender, enemies, sd) in facts["hq_pressure"]:
        press.setdefault(defender, {})[tt] = (set(enemies), sd)
    assaults = []
    for defender, tmap in press.items():
        attacker = other[defender]
        win = []
        for t in sorted(tmap):
            if win and t - win[-1] <= 2:
                win.append(t)
            else:
                if win:
                    assaults.append(_assault_summary(facts, defender, attacker, win, tmap, ASSAULT_K, hqreg))
                win = [t]
        if win:
            assaults.append(_assault_summary(facts, defender, attacker, win, tmap, ASSAULT_K, hqreg))
    assaults = [a for a in assaults if a]

    # 순손해 교전 (COMBAT+TURRET 피해 교환)
    recv = {"A": {}, "B": {}}
    for (t, side, _uid, _cause, _r, amt) in facts["combat_log"]:
        recv[side][t] = recv[side].get(t, 0) + (amt or 0)
    tot = {"A": sum(recv["A"].values()), "B": sum(recv["B"].values())}
    net = []
    for t in sorted(set(list(recv["A"]) + list(recv["B"]))):
        for side in ("A", "B"):
            dealt, received = recv[other[side]].get(t, 0), recv[side].get(t, 0)
            if dealt - received < 0:
                net.append((t, side, dealt, received, dealt - received))
    worst = sorted(net, key=lambda x: x[4])[:10]

    # 정크 무빙 (경로 재방문/왕복)
    junk = []
    for side in ("A", "B"):
        for uid, u in facts["units"][side].items():
            comp, seen, revisit = [], {}, 0
            for (_tt, r) in u.path:
                if not comp or comp[-1] != r:
                    comp.append(r)
            for r in comp:
                seen[r] = seen.get(r, 0) + 1
                if seen[r] >= 2:
                    revisit += 1
            if revisit >= 2:
                junk.append((side, uid, revisit, len(comp)))
    junk.sort(key=lambda x: x[2], reverse=True)

    # 과소·자살 파병 + 거점 선점 레이스 (턴별 점령 재구성 기반)
    end_turn = pt[-1]["turn"] if pt else 0
    occ = _build_occupancy(facts, end_turn)
    strongholds = set(facts.get("strongholds", []))
    bld = facts["buildings"]

    def enemy_bld_at(owner, region, t):
        """region 에 owner 건물이 t 시점 살아있으면 (level, turret), 아니면 None."""
        b = bld[owner].get(region)
        if b is None or b.built_turn > t:
            return None
        if b.destroyed_turn is not None and b.destroyed_turn <= t:
            return None
        lvl, _inv = _level_at(facts, owner, region, t, is_hq=b.is_hq)
        lvl = max(1, lvl)
        turret = (HQ if b.is_hq else BASE)[lvl]["turret"]
        return (lvl, turret)

    # --- 파병 웨이브 클러스터링(같은 목적지·근접 발령턴) → 과소·자살 파병 ---
    bykey = defaultdict(list)
    for (t, side, uid, dest, own) in facts.get("dispatches", []):
        if not own:                      # 아군 건물행(무료 rally)은 파병 판정에서 제외
            bykey[(side, dest)].append((t, uid))
    DWIN = 2
    doomed = []
    for (side, dest), lst in bykey.items():
        lst.sort()
        clusters, cur = [], []
        for (t, uid) in lst:
            if cur and t - cur[-1][0] > DWIN:
                clusters.append(cur)
                cur = []
            cur.append((t, uid))
        if cur:
            clusters.append(cur)
        enemy = other[side]
        for cl in clusters:
            issue_t = cl[0][0]
            arrivals = []
            for (_t, uid) in cl:
                u = facts["units"][side].get(uid)
                if not u:
                    continue
                a = next((tt for (tt, r) in u.path if r == dest and tt >= issue_t), None)
                if a is not None:
                    arrivals.append((uid, a))
            if len(arrivals) < 2:        # 단발 찌르기는 net_loss/objective_race가 처리
                continue
            our_force = len(arrivals)
            t_arr = sorted(a for (_u, a) in arrivals)[len(arrivals) // 2]
            gar_issue = _garrison(occ, enemy, dest, issue_t)
            gar_arr = max((_garrison(occ, enemy, dest, tt) for tt in range(t_arr - 1, t_arr + 2)), default=0)
            garrison = max(gar_issue, gar_arr)
            eb = enemy_bld_at(enemy, dest, t_arr)
            losses = sum(1 for (uid, a) in arrivals
                         if (lambda u: u and u.death_turn is not None
                             and a <= u.death_turn <= a + 3
                             and u.death_cause in ("COMBAT", "TURRET"))(facts["units"][side].get(uid)))
            # 플래그: 도착 병력 ≤ 적 수비 & 실제 전사 & (열세이거나 요새(건물)로 향함)
            if our_force <= garrison and losses >= 1 and (garrison > our_force or eb is not None):
                doomed.append({"t": issue_t, "side": side, "dest": dest, "arr": t_arr,
                               "force": our_force, "garrison": garrison, "gar_issue": gar_issue,
                               "enemy_bld": eb, "losses": losses, "stronghold": dest in strongholds})
    doomed.sort(key=lambda d: d["t"])

    # --- 거점 선점 레이스(초반 establish): 먼저 건설한 쪽이 승, 진 쪽 지연 파병이 소모됐나 ---
    race = []
    for R in sorted(strongholds):
        fb = {"A": None, "B": None}
        for (t, s, r, action, _lvl, _cost) in facts["building_events"]:
            if r == R and action == "build" and fb[s] is None:
                fb[s] = t
        builders = [s for s in ("A", "B") if fb[s] is not None]
        if len(builders) != 1:           # 아무도/둘 다 건설 → establish 승자 불명확, 보류
            continue
        winner = builders[0]
        loser = other[winner]
        tb = fb[winner]
        fp = {"A": None, "B": None}
        for t in range(0, tb + 9):
            d = occ.get(t)
            if not d:
                continue
            for s in ("A", "B"):
                if fp[s] is None and d[s].get(R):
                    fp[s] = t
        wasted = []
        for uid, u in facts["units"][loser].items():
            a = next((tt for (tt, r) in u.path if r == R), None)
            if a is None or not (tb - 2 <= a <= tb + 8):
                continue
            if u.death_turn is not None and a <= u.death_turn <= a + 4 and u.death_cause in ("COMBAT", "TURRET"):
                wasted.append((uid, a, u.spawn_turn))
        if wasted:
            race.append({"R": R, "winner": winner, "loser": loser, "opp_build": tb,
                         "opp_arr": fp[winner], "our_arr": fp[loser],
                         "wasted": sorted(wasted, key=lambda x: x[1])})
    race.sort(key=lambda x: x["opp_build"])

    # --- 후퇴 헌납: 주둔을 아군 건물로 뺀 직후 기지가 무저항으로 파괴된 케이스 ---
    RC_BACK, RC_FWD, RC_GMIN = 6, 12, 3
    retreat_cede = []
    for side in ("A", "B"):
        enemy = other[side]
        for r, b in bld[side].items():
            td = b.destroyed_turn
            if td is None or b.is_hq:
                continue
            g_before = max((_garrison(occ, side, r, t)
                            for t in range(max(0, td - RC_BACK), td)), default=0)
            if g_before < RC_GMIN:
                continue
            if _garrison(occ, side, r, td) > 1:
                continue                      # 수비가 남아 싸우다 짐 → 순손해 교전 소관
            # 철수 = 창 내 발령, 발령 시 위치 r, 목적지가 (다른) 아군 건물인 디스패치
            pulled = {}
            for (t, s, uid, dest, own) in facts.get("dispatches", []):
                if s != side or not own or dest == r or not (td - RC_BACK <= t <= td):
                    continue
                d0, d1 = occ.get(t - 1), occ.get(t)
                if ((d0 and uid in d0[side].get(r, []))
                        or (d1 and uid in d1[side].get(r, []))):
                    pulled[uid] = (t, dest)
            if len(pulled) < max(2, g_before - 1):
                continue                      # 대부분이 빠졌을 때만(공격 출격 own=False는 애초 제외)
            t_pull = min(t for (t, _d) in pulled.values())
            dests = {}
            for (_t, d_) in pulled.values():
                dests[d_] = dests.get(d_, 0) + 1
            main_dest = max(dests, key=dests.get)
            wave = max((_garrison(occ, enemy, r, t) for t in range(td - 1, td + 2)), default=0)
            lvl_td, invested = _level_at(facts, side, r, td)
            lvl_td = max(1, lvl_td)
            # 사후 교전: 철수 병력이 짧은 창 내 어차피 싸웠나 (요격 지점만 뒤로 민 정황)
            fight_later = None
            pulled_set = set(pulled)
            for (t, cs, uid, cause, cr, _amt) in facts["combat_log"]:
                if not (td < t <= td + RC_FWD) or cause != "COMBAT":
                    continue
                if cs == side and uid in pulled_set:
                    fight_later = (t, cr)
                    break
                if cs == enemy and cr is not None:
                    d = occ.get(t)
                    here = d[side].get(cr, []) if d else []
                    if sum(1 for u2 in here if u2 in pulled_set) >= 3:
                        fight_later = (t, cr)
                        break
            retreat_cede.append({"t_pull": t_pull, "side": side, "R": r, "level": lvl_td,
                                 "invested": invested, "garrison": g_before,
                                 "pulled": len(pulled), "dest": main_dest, "wave": wave,
                                 "destroyed": td, "fight_later": fight_later,
                                 "stronghold": r in strongholds})
    retreat_cede.sort(key=lambda d: d["destroyed"])

    sig["orders"] = {"train_burst": train_burst, "move_burst": move_burst,
                     "income_drop": income_drop, "rally_no_threat": rally_no_threat,
                     "futile_train": futile_train, "assaults": assaults,
                     "trade_total": tot, "net_loss_turns": worst, "junk": junk[:12],
                     "doomed_dispatch": doomed, "objective_race": race,
                     "retreat_cede": retreat_cede}
    return sig


def render(facts, basename):
    L = []
    w = L.append
    n = facts["n"]
    res = facts["result"]
    pt = facts["per_turn"]
    end_turn = pt[-1]["turn"] if pt else 0
    sig = derive_signals(facts)

    # 골드 신뢰 경계 계산: 언더플로/드리프트 이후는 하한(실제 >= 표기)
    hf = facts["hunger_flags"]
    hard = [f for f in hf if f[4] == "hard"]
    drift = [f for f in hf if f[4] == "drift"]
    uf = facts["gold_underflow"]
    reliable_until = {}
    for side in ("A", "B"):
        marks = [f[0] for f in drift if f[1] == side] + [u[0] for u in uf if u[1] == side]
        reliable_until[side] = (min(marks) - 1) if marks else end_turn

    w("# FACTS: %s" % basename)
    w("")
    w("> replay.py 자동 생성. 병력/위치/전투/공성/건물/타이밍/궤적 = **로그 실측(정확)**.")
    w("> 골드 = 규칙 역산치: t%d(A)/t%d(B)까지 **검증된 정확치**, 이후는 **하한(실제 ≥ 표기, drift)**."
      % (reliable_until["A"], reliable_until["B"]))
    w("> 진영: A = LEFT(본부 구역 0), B = RIGHT(본부 구역 %d). '우리/상대' 매핑은 파일명 태그로 판정." % (n - 1))
    w("")

    # 결과
    w("## 결과")
    w("- RESULT: `%s %s`" % (res.get("outcome", ""), res.get("reason", "")))
    w("- 종료 턴: %d (맵 N=%d, K=%d)" % (end_turn, n, facts["k"]))
    if pt:
        last = pt[-1]
        w("- 최종 본부 HP: A=%d(L%d), B=%d(L%d)"
          % (last["A"]["hq_hp"], last["A"]["hq_level"], last["B"]["hq_hp"], last["B"]["hq_level"]))
        w("- 최종 골드(역산): A=%d, B=%d" % (max(0, last["A"]["gold"]), max(0, last["B"]["gold"])))
        w("- 최종 생존 전사: A=%d, B=%d / 건물: A=%d, B=%d"
          % (last["A"]["alive"], last["B"]["alive"], last["A"]["buildings"], last["B"]["buildings"]))
    # 승패 조건
    if sig["cond_type"] == "combat_finish":
        f = sig["finisher"]
        if f:
            w("- **승패 조건: 전투 마무리(본부 파괴)** — 패배 %s 본부 t%d 파괴. 마무리 공격조 %s(%d기): %s"
              % (f["loser"], f["turn"], f["attacker"], len(f["units"]), " ".join(f["units"]) or "-"))
        else:
            w("- **승패 조건: 전투 마무리(본부 파괴)**")
    elif sig["cond_type"] == "hq_hp":
        if pt:
            last = pt[-1]
            margin = last["A"]["hq_hp"] - last["B"]["hq_hp"]
            w("- **승패 조건: 200턴 도달 → 본부 HP 판정** (A %d L%d vs B %d L%d, 마진 A−B=%+d) → 성장격차 귀인은 아래 참조."
              % (last["A"]["hq_hp"], last["A"]["hq_level"], last["B"]["hq_hp"], last["B"]["hq_level"], margin))
    else:
        w("- **승패 조건: `%s`**" % sig["cond_type"])
    w("")

    # 검증
    w("## 검증 (self-check)")
    # HARD: 로그에 기아가 있는데 예측 못함 = 골드 과대추정 = 진짜 모델 오류(0이어야 정상)
    if hard:
        w("- ⚠️ HARD 오류 **%d건** (로그 HUNGER 있는데 예측 못함 = 골드 과대추정). 모델 점검 필요:" % len(hard))
        for (t, s, p, a, _k) in hard[:15]:
            w("  - t%d %s: 예측 기아 %d < 로그 HUNGER %d" % (t, s, p, a))
    else:
        w("- HARD 오류: **0건** (로그가 실제 기아를 보인 지점은 모두 재현). 골드 상한 신뢰 OK.")
    # DRIFT: 예측만 기아이고 로그엔 없음 = 골드 과소추정 = 이후 골드는 하한
    if drift:
        w("- DRIFT %d건 (골드 과소추정으로 예측만 기아). 해당 이후 골드는 하한값." % len(drift))
    if uf:
        first_uf = {}
        for (t, s, need, have) in uf:
            first_uf.setdefault(s, (t, need, have))
        for s in ("A", "B"):
            if s in first_uf:
                t, need, have = first_uf[s]
                w("- 골드 언더플로 %s: t%d에 로그 지출 %d > 역산 보유 %d → 실제 골드가 더 많았음(수입 과소 등)."
                  % (s, t, need, have))
    w("- 골드 신뢰 경계: A t%d까지 정확, B t%d까지 정확 (이후 하한)."
      % (reliable_until["A"], reliable_until["B"]))
    warn = facts["warnings"]
    w("- 파싱 경고: %d건%s" % (len(warn), ("" if not warn else " (" + ", ".join(sorted({c for (_, c, _) in warn})) + ")")))
    # HQ 파괴 정합성
    if res.get("reason") == "HQ_DESTROYED":
        dead = [s for s in ("A", "B") if facts["buildings"][s][facts["hq_region"][s]].destroyed_turn is not None]
        w("- HQ 파괴 정합성: 파괴된 본부 = %s (승패 결과와 대조)" % (dead or "없음(경고: 결과와 불일치)"))
    w("")

    # 주요 타이밍
    w("## 주요 타이밍 (턴)")
    w("| 항목 | A | B |")
    w("|---|--:|--:|")
    tk = facts["timings"]
    for key, label in [("first_move", "첫 이동"), ("first_train", "첫 훈련"),
                       ("first_base", "첫 기지 건설"), ("first_upgrade", "첫 건설/업글"),
                       ("first_combat", "첫 전투피해"), ("first_siege_against", "첫 피공성")]:
        w("| %s | %s | %s |" % (label, tk["A"].get(key, "-"), tk["B"].get(key, "-")))
    w("")

    # 경제/상태 턴별 표
    w("## 턴별 경제·병력 (골드=역산, 나머지=실측)")
    w("| 턴 | A골드 | A병력 | A건물 | A수입 | A지출(이/훈/업) | B골드 | B병력 | B건물 | B수입 | B지출(이/훈/업) |")
    w("|--:|--:|--:|--:|--:|:--|--:|--:|--:|--:|:--|")
    for s in pt:
        a, b = s["A"], s["B"]
        w("| %d | %d | %d | %d | %d | %d/%d/%d | %d | %d | %d | %d | %d/%d/%d |" % (
            s["turn"],
            max(0, a["gold"]), a["alive"], a["buildings"], a["income"],
            a["spend_move"], a["spend_train"], a["spend_upgrade"],
            max(0, b["gold"]), b["alive"], b["buildings"], b["income"],
            b["spend_move"], b["spend_train"], b["spend_upgrade"]))
    w("")

    # 건물 타임라인
    w("## 건물 타임라인 (건설/업글/수리)")
    if facts["building_events"]:
        w("| 턴 | 진영 | 구역 | 동작 | 레벨 | 비용 |")
        w("|--:|:--|--:|:--|--:|--:|")
        for (t, s, r, act, lv, cost) in facts["building_events"]:
            w("| %d | %s | %d | %s | %d | %d |" % (t, s, r, act, lv, cost))
    else:
        w("(없음)")
    w("")

    # 본진 압박 타임라인 (핵심: 본진 러쉬 규모·출발지 추적)
    w("## 본진 압박 타임라인 (적 유닛이 우리 본부 구역에 진입 / 공성)")
    hp = facts["hq_pressure"]
    if hp:
        w("| 턴 | 방어측 | 본부구역 적 유닛 수 | 적 유닛 | 이번 턴 공성 |")
        w("|--:|:--|--:|:--|--:|")
        for (t, defender, enemies, sd) in hp:
            hr = facts["hq_region"][defender]
            w("| %d | %s (구역 %d) | %d | %s | %d |"
              % (t, defender, hr, len(enemies), " ".join(enemies) if enemies else "-", sd))
        w("")
        w("> 위 '적 유닛'의 출발지·경로는 아래 유닛 궤적표에서 해당 id로 추적할 것.")
    else:
        w("(본부 구역 진입/공성 없음)")
    w("")

    # 유닛 궤적 (출발지 추적용)
    w("## 유닛 궤적 (출발지·경로·전사)")
    w("| id | 스폰턴 | 스폰구역 | 사망(턴/원인) | 경로(구역@턴) |")
    w("|:--|--:|--:|:--|:--|")
    for side in ("A", "B"):
        for uid in sorted(facts["units"][side].keys(), key=lambda x: (x[0], int(x[1:]) if x[1:].isdigit() else 0)):
            u = facts["units"][side][uid]
            death = ("%d/%s" % (u.death_turn, u.death_cause)) if u.death_turn else "생존"
            w("| %s | %d | %d | %s | %s |" % (uid, u.spawn_turn, u.spawn_region, death, _compress_path(u.path)))
    w("")

    # 큰 피해 턴 Top
    w("## 피해 집중 턴 (전사 피해 Top 12)")
    agg = {}
    for (t, s, uid, cause, r, amt) in facts["combat_log"]:
        agg[(t, s)] = agg.get((t, s), 0) + (amt or 0)
    top = sorted(agg.items(), key=lambda kv: kv[1], reverse=True)[:12]
    if top:
        w("| 턴 | 진영 | 받은 전사피해 합 |")
        w("|--:|:--|--:|")
        for ((t, s), a) in top:
            w("| %d | %s | %d |" % (t, s, a))
    else:
        w("(없음)")
    w("")

    # 성장 격차 (특히 200턴 본부HP 판정 귀인용)
    g = sig["growth"]
    w("## 성장 격차 (건물수·누적수입 → 200턴 판정 귀인)")
    w("- 최종 건물수 격차 A−B = %+d (정착 턴 t%s: 이후 부호 유지)"
      % (g["build_gap_final"], g["build_settle"] if g["build_settle"] is not None else "-"))
    w("- 최종 누적수입 격차 A−B = %+d (정착 턴 t%s)"
      % (g["cumincome_gap_final"], g["cumincome_settle"] if g["cumincome_settle"] is not None else "-"))
    if g["quartiles"]:
        w("| 지점 | 건물 A:B | 병력 A:B | 본부Lv A:B | 본부HP A:B |")
        w("|--:|:--|:--|:--|:--|")
        for q in g["quartiles"]:
            w("| t%d | %d:%d | %d:%d | %d:%d | %d:%d |"
              % (q["turn"], q["build"][0], q["build"][1], q["alive"][0], q["alive"][1],
                 q["hq_level"][0], q["hq_level"][1], q["hq_hp"][0], q["hq_hp"][1]))
    w("")

    # 오더 품질 신호 (중립 관찰 — '손해 오더' 판정은 POLICY 루브릭으로 LLM 수행)
    o = sig["orders"]
    w("## 오더 품질 신호 (관찰 — 손해 판정은 LLM)")
    w("> 아래는 결정론적 관찰 신호일 뿐이다. 각 항목이 실제 '무의미/손해 오더'인지는 맥락(직후 효과·")
    w("> 승패 영향)으로 POLICY 루브릭에 따라 판정하고, 봇 내부기능 추측은 '후보/관찰'로 캐버트할 것.")

    w("")
    w("### 헛발동 후보 — 위협 없는 훈련 폭증 (보수적 단정)")
    if o["futile_train"]:
        for (t, side, c) in o["futile_train"][:10]:
            w("- t%d %s: 훈련 폭증 %d기인데 이후 8턴까지 %s 본부권 전투/공성 없음 → 헛발동 긴급훈련 후보" % (t, side, c, side))
    else:
        w("- (없음: 위협 없는 훈련 폭증 미검출)")
    # 중립 관찰 (단정 금지 — 궤적/맥락으로 LLM 판정)
    if o["rally_no_threat"]:
        rr = ", ".join("t%d %s×%d" % (t, s, c) for (t, s, c) in o["rally_no_threat"][:12])
        w("- (중립) 본부 복귀/집결 이동(위협 없음): %s — **회군(손해) or 채굴 복귀(정상) 구분은 궤적으로**." % rr)
    if o["income_drop"]:
        w("- (중립) 수입 급락 턴 %d회 — 워커 재배치 정황(공격/확장/방어 모두 가능, 헛발동 단정 금지)." % len(o["income_drop"]))

    w("")
    w("### 본진 공격 시도·결과")
    if o["assaults"]:
        w("| 창(턴) | 공격측 | 피크 규모 | 결과 | 투입 유닛 |")
        w("|:--|:--|--:|:--|:--|")
        for a in sorted(o["assaults"], key=lambda x: x["start"]):
            out = "성공(본부 파괴)" if a["success"] else ("실패/과소: %d기 중 %d기 전사" % (len(a["units"]), a["losses"]))
            w("| t%d~%d | %s | %d | %s | %s |"
              % (a["start"], a["end"], a["attacker"], a["peak"], out, " ".join(a["units"][:12]) or "-"))
    else:
        w("(적 본부권 진입 시도 없음)")

    w("")
    w("### 순손해 교전 (COMBAT+TURRET 피해 교환)")
    tt = o["trade_total"]
    w("- 누적 받은 피해: A=%d, B=%d (적을수록 유리). 가한 피해 = 상대가 받은 피해." % (tt["A"], tt["B"]))
    if o["net_loss_turns"]:
        w("- 순손해 큰 턴(가한−받은 < 0):")
        for (t, side, dealt, received, net_) in o["net_loss_turns"]:
            w("  - t%d %s: 가함 %d / 받음 %d (순 %+d)" % (t, side, dealt, received, net_))

    w("")
    w("### 대량 이동·훈련 폭증 턴 (신호만)")
    mb = ", ".join("t%d %s×%d" % (t, s, c) for (t, s, c) in o["move_burst"][:12])
    tb = ", ".join("t%d %s×%d" % (t, s, c) for (t, s, c) in o["train_burst"][:12])
    w("- 대량 이동(신규 MOVE ≥5): %s" % (mb or "없음"))
    w("- 훈련 폭증(TRAIN ≥2): %s" % (tb or "없음"))

    w("")
    w("### 정크 무빙 후보 (경로 왕복/재방문 ≥2)")
    if o["junk"]:
        w("| 유닛 | 재방문 횟수 | 경로 구역 수 |")
        w("|:--|--:|--:|")
        for (side, uid, rev, ln) in o["junk"]:
            w("| %s | %d | %d |" % (uid, rev, ln))
        w("> 각 유닛의 실제 경로는 유닛 궤적표 참조. 왕복=이동비 낭비/회군 정황(단정 말 것).")
    else:
        w("(뚜렷한 왕복 이동 없음)")

    w("")
    w("### 과소·자살 파병 (도착 병력 ≤ 목적지 적 수비인데 보낸 파병)")
    if o.get("doomed_dispatch"):
        w("| 발령 t | 목적지 | 아군 도착 | 적 수비(주둔) | 적 건물 | 아군 전사 |")
        w("|--:|--:|--:|--:|:--|--:|")
        for d in o["doomed_dispatch"][:12]:
            loc = "거점%d" % d["dest"] if d["stronghold"] else "구역%d" % d["dest"]
            ebs = ("L%d·포탑%d" % d["enemy_bld"]) if d["enemy_bld"] else "-"
            w("| t%d | %s | %d기 | %d (발령시 %d) | %s | %d |"
              % (d["t"], loc, d["force"], d["garrison"], d["gar_issue"], ebs, d["losses"]))
        w("> 도착 병력이 목적지의 적 수비 이하인 파병(수비오더와 겹쳐 일부만 전진한 경우 포함) →")
        w("> 도착 즉시 순손해. **도착 병력 ≤ 수비면 명백 손해로 단정 가능**(POLICY §4.4).")
    else:
        w("(도착 병력이 적 수비 이하인 파병 미검출)")

    w("")
    w("### 거점 선점 레이스 지연 (템포 손해 후보)")
    if o.get("objective_race"):
        for r in o["objective_race"][:8]:
            oa = "t%d" % r["opp_arr"] if r["opp_arr"] is not None else "t?"
            ua = "t%d" % r["our_arr"] if r["our_arr"] is not None else "t?"
            wl = " ".join("%s(도착t%d,생성t%d)" % (u, a, sp) for (u, a, sp) in r["wasted"][:6])
            w("- 거점%d: **%s 선점**(건설 t%d, 첫도착 %s) vs 우리(%s) 첫도착 %s — 진 뒤 지연 파병 소모: %s"
              % (r["R"], r["winner"], r["opp_build"], oa, r["loser"], ua, wl))
        w("> 상대가 거점을 먼저 점령·건설한 뒤 우리 뒤늦은 파병이 그 수비/포탑에 전사한 케이스.")
        w("> **유휴=채굴/신병 딜레이일 수 있어 후보로만** — 궤적표로 출발 지연(즉시출발 대신 신병 파병 등) 확인.")
    else:
        w("(거점 선점 레이스에서 진 뒤 지연 파병 소모 미검출)")

    w("")
    w("### 후퇴 헌납 후보 (주둔 철수 직후 기지 무저항 상실)")
    if o.get("retreat_cede"):
        w("| 철수 t | 기지 | 직전 주둔 | 철수 → 행선 | 적 웨이브 | 파괴 t | 이후 교전(철수 병력) |")
        w("|--:|:--|--:|:--|--:|--:|:--|")
        for d in o["retreat_cede"][:8]:
            loc = ("거점%d" % d["R"]) if d["stronghold"] else ("구역%d" % d["R"])
            fl = ("t%d @구역%d — **어차피 싸움**" % d["fight_later"]) if d["fight_later"] \
                else "없음(+%d턴 창)" % 12
            w("| t%d | %s L%d(투자 %dg) | %d기 | %d기 → 구역%d | %d기 | t%d | %s |"
              % (d["t_pull"], loc, d["level"], d["invested"], d["garrison"],
                 d["pulled"], d["dest"], d["wave"], d["destroyed"], fl))
        w("> 주둔을 아군 건물로 뺀 직후 기지가 무저항 파괴된 케이스. 철수=각개격파 회피(정상)일 수 있으나,")
        w("> **이후 짧은 창 내 같은 웨이브와 어차피 교전했으면(이후 교전 有) 요격 지점만 뒤로 민 것 —")
        w("> 기지·포탑 지원만 공짜 상실 = 손해 판정 가능**(POLICY §4.4). 적이 유도(베이트) 가능한 패턴.")
    else:
        w("(주둔 철수 직후 기지 상실 미검출)")
    w("")

    return "\n".join(L) + "\n"


def to_json(facts, basename):
    units_out = {}
    for side in ("A", "B"):
        units_out[side] = {
            u.uid: {
                "spawn_turn": u.spawn_turn, "spawn_region": u.spawn_region,
                "death_turn": u.death_turn, "death_cause": u.death_cause,
                "path": u.path,
            } for u in facts["units"][side].values()
        }
    return json.dumps({
        "basename": basename,
        "n": facts["n"], "k": facts["k"], "hq_region": facts["hq_region"],
        "strongholds": facts["strongholds"],
        "result": facts["result"],
        "per_turn": facts["per_turn"],
        "timings": facts["timings"],
        "building_events": facts["building_events"],
        "hq_pressure": facts["hq_pressure"],
        "hunger_flags": facts["hunger_flags"],
        "warnings": facts["warnings"],
        "signals": derive_signals(facts),
        "units": units_out,
    }, ensure_ascii=False, indent=1)


def main(argv=None):
    ap = argparse.ArgumentParser(description="NEXT NATION 로그 -> facts sheet")
    ap.add_argument("log", help="raw 로그 txt 경로")
    ap.add_argument("--out", help="facts markdown 출력 경로 (미지정 시 stdout)")
    ap.add_argument("--json", dest="json_out", help="facts JSON 출력 경로 (선택)")
    args = ap.parse_args(argv)

    with open(args.log, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    pr = parse_log(text)
    facts = replay(pr)
    basename = os.path.splitext(os.path.basename(args.log))[0]
    md = render(facts, basename)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(md)
    else:
        sys.stdout.reconfigure(encoding="utf-8") if hasattr(sys.stdout, "reconfigure") else None
        sys.stdout.write(md)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            f.write(to_json(facts, basename))

    # 종료코드: HARD 오류(로그 기아를 예측 못함=골드 과대추정)가 있으면 1. DRIFT/언더플로는 하한 표기라 0.
    hard = [f for f in facts["hunger_flags"] if f[4] == "hard"]
    return 1 if hard else 0


if __name__ == "__main__":
    raise SystemExit(main())

from __future__ import annotations

from collections import defaultdict

from .models import Game
from .rules_config import BUILDING_LEVELS, HQ_INITIAL_UNITS, MAX_BUILDING_LEVEL


def replay_state(game: Game) -> dict:
    buildings = {
        "A": {0: {"level": 1, "first_turn": 0}},
        "B": {game.map.n - 1: {"level": 1, "first_turn": 0}} if game.map.n else {},
    }
    units = {side: {} for side in ("A", "B")}
    for side, unit_ids in HQ_INITIAL_UNITS.items():
        hq = 0 if side == "A" else max(game.map.n - 1, 0)
        for uid in unit_ids:
            units[side][uid] = {"first_turn": 0, "last_turn": 0, "location": hq, "hp": 4, "active": True, "path": [hq]}

    snapshots = []
    damage_by_unit = defaultdict(int)
    combat_zones = []
    for turn in game.turns:
        trained = {"A": 0, "B": 0}
        losses = {"A": 0, "B": 0}
        acquired = {"A": 0, "B": 0}
        upgrades = {"A": 0, "B": 0}
        damage = {"A": 0, "B": 0}
        siege = {"A": 0, "B": 0}
        for event in turn.events:
            side = event.side
            if event.type == "TRAIN" and side and event.unit_id:
                hq_level = buildings[side].get(0 if side == "A" else game.map.n - 1, {}).get("level", 1)
                hp = BUILDING_LEVELS.get(hq_level, BUILDING_LEVELS[1])["unit_hp"]
                loc = 0 if side == "A" else game.map.n - 1
                units[side][event.unit_id] = {
                    "first_turn": turn.turn_no,
                    "last_turn": turn.turn_no,
                    "location": loc,
                    "hp": hp,
                    "active": True,
                    "path": [loc],
                }
                trained[side] += 1
            elif event.type == "MOVE" and side and event.unit_id and event.location is not None:
                unit = units[side].setdefault(
                    event.unit_id,
                    {
                        "first_turn": turn.turn_no,
                        "last_turn": turn.turn_no,
                        "location": event.location,
                        "hp": 4,
                        "active": True,
                        "path": [],
                    },
                )
                unit["location"] = event.location
                unit["last_turn"] = turn.turn_no
                unit["path"].append(event.location)
            elif event.type == "UPGRADE" and side and event.location is not None:
                before = buildings[side].get(event.location)
                if before is None:
                    buildings[side][event.location] = {"level": 1, "first_turn": turn.turn_no}
                    acquired[side] += 1
                else:
                    if before["level"] < MAX_BUILDING_LEVEL:
                        before["level"] += 1
                    upgrades[side] += 1
            elif event.type == "DAMAGE" and side and event.unit_id:
                dmg = event.amount or 0
                damage[side] += dmg
                damage_by_unit[event.unit_id] += dmg
                unit = units[side].get(event.unit_id)
                location = unit.get("location") if unit else None
                combat_zones.append(
                    {
                        "turn": turn.turn_no,
                        "side": side,
                        "unit_id": event.unit_id,
                        "cause": event.cause,
                        "location": location,
                        "amount": dmg,
                    }
                )
                if unit and unit.get("active", True):
                    unit["hp"] -= dmg
                    unit["last_turn"] = turn.turn_no
                    if unit["hp"] <= 0:
                        unit["active"] = False
                        losses[side] += 1
            elif event.type == "SIEGE" and side:
                siege[side] += event.amount or 0
        snapshots.append(
            {
                "turn": turn.turn_no,
                "alive": {s: sum(1 for u in units[s].values() if u.get("active", True)) for s in ("A", "B")},
                "trained": trained,
                "losses": losses,
                "building_count": {s: len(buildings[s]) for s in ("A", "B")},
                "building_acquired": acquired,
                "upgrade_count": upgrades,
                "damage_taken": damage,
                "siege_done": siege,
            }
        )
    return {
        "buildings": buildings,
        "units": units,
        "snapshots": snapshots,
        "damage_by_unit": dict(damage_by_unit),
        "combat_zones": combat_zones,
    }

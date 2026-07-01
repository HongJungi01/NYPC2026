from __future__ import annotations

from .models import Game
from .rules_config import BUILDING_LEVELS, INITIAL_GOLD, MOVE_COST, NEW_BUILDING_COST, TRAIN_COST


def estimate_gold(game: Game, replay: dict) -> tuple[list[dict], list[dict]]:
    gold = {"A": INITIAL_GOLD, "B": INITIAL_GOLD}
    buildings = {"A": {0: 1}, "B": {game.map.n - 1: 1} if game.map.n else {}}
    rows: list[dict] = []
    details: list[dict] = []
    for turn in game.turns:
        per_side = {
            s: {
                "confirmed_income": 0,
                "estimated_income": 0,
                "confirmed_spend": 0,
                "estimated_spend": 0,
                "train_spend": 0,
                "move_spend": 0,
                "upgrade_spend": 0,
                "maintenance_spend": 0,
                "income_from_buildings": 0,
                "income_from_workers": 0,
                "evidence_events": [],
                "gold_confidence": 0.7,
            }
            for s in ("A", "B")
        }
        for side, commands in turn.commands.items():
            for command in commands:
                if command.type == "MOVE" and command.value is not None:
                    if command.value not in buildings.get(side, {}):
                        per_side[side]["confirmed_spend"] += MOVE_COST
                        per_side[side]["move_spend"] += MOVE_COST
                        per_side[side]["evidence_events"].append(f"TURN {turn.turn_no} {command.raw}")
        for event in turn.events:
            side = event.side
            if not side:
                continue
            if event.type == "TRAIN":
                per_side[side]["confirmed_spend"] += TRAIN_COST
                per_side[side]["train_spend"] += TRAIN_COST
                per_side[side]["evidence_events"].append(f"TURN {turn.turn_no} {event.raw}")
            elif event.type == "UPGRADE" and event.location is not None:
                before = buildings[side].get(event.location)
                cost = NEW_BUILDING_COST if before is None else BUILDING_LEVELS.get(before + 1, BUILDING_LEVELS[5])["upgrade_cost"]
                per_side[side]["confirmed_spend"] += cost
                per_side[side]["upgrade_spend"] += cost
                buildings[side][event.location] = 1 if before is None else min(before + 1, 5)
                per_side[side]["evidence_events"].append(f"TURN {turn.turn_no} {event.raw}")
            elif event.type == "DAMAGE" and event.cause == "HUNGER":
                per_side[side]["maintenance_spend"] += 1
                per_side[side]["gold_confidence"] = 0.55
        snap = next((s for s in replay["snapshots"] if s["turn"] == turn.turn_no), None)
        for side in ("A", "B"):
            building_income = sum(BUILDING_LEVELS.get(level, BUILDING_LEVELS[1])["income"] for level in buildings[side].values())
            workers = snap["alive"][side] if snap else 0
            worker_income = min(workers, sum(BUILDING_LEVELS.get(level, BUILDING_LEVELS[1])["worker_capacity"] for level in buildings[side].values())) * 5
            per_side[side]["estimated_income"] = building_income + worker_income
            per_side[side]["income_from_buildings"] = building_income
            per_side[side]["income_from_workers"] = worker_income
            gold[side] += per_side[side]["estimated_income"] - per_side[side]["confirmed_spend"]
            row = {
                "turn": turn.turn_no,
                "side": side,
                **per_side[side],
                "min_estimated_gold": min(gold[side], gold[side] - 30),
                "max_estimated_gold": max(gold[side], gold[side] + 30),
            }
            row["evidence_events"] = " | ".join(row["evidence_events"])
            rows.append(row)
            details.append({"turn": turn.turn_no, "side": side, "estimated_gold": gold[side], "basis": row["evidence_events"]})
    return rows, details


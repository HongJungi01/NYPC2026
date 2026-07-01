from __future__ import annotations

from collections import Counter, defaultdict, deque

from .models import Game


def compute_metrics(game: Game, replay: dict, gold_rows: list[dict]) -> dict:
    command_counts = {"A": Counter(), "B": Counter()}
    event_counts = {"A": Counter(), "B": Counter()}
    first_turns: dict[str, dict[str, int | None]] = {
        "A": {"TRAIN": None, "MOVE": None, "UPGRADE": None, "SIEGE": None, "COMBAT": None},
        "B": {"TRAIN": None, "MOVE": None, "UPGRADE": None, "SIEGE": None, "COMBAT": None},
    }
    time_by_turn = []
    events_rows = []
    commands_rows = []

    for turn in game.turns:
        for side, commands in turn.commands.items():
            for command in commands:
                command_counts[side][command.type] += 1
                commands_rows.append(command.to_dict())
        time_row = {"turn": turn.turn_no}
        for event in turn.events:
            if event.side in ("A", "B"):
                event_counts[event.side][event.type] += 1
                if first_turns[event.side].get(event.type) is None:
                    first_turns[event.side][event.type] = turn.turn_no
                if event.type == "DAMAGE" and event.cause == "COMBAT" and first_turns[event.side]["COMBAT"] is None:
                    first_turns[event.side]["COMBAT"] = turn.turn_no
                if event.type == "TIME":
                    time_row[f"{event.side}_time_used_ms"] = event.time_used_ms
                    time_row[f"{event.side}_time_tokens"] = event.time_tokens
            events_rows.append(event.to_dict())
        if len(time_row) > 1:
            time_by_turn.append(time_row)

    shortest = _shortest_distances(game.map.adjacency, 0) if game.map.n else []
    shortest_from_b = _shortest_distances(game.map.adjacency, game.map.n - 1) if game.map.n else []
    map_metrics = [
        {
            "region": i,
            "distance_from_A_hq": shortest[i] if i < len(shortest) else None,
            "distance_from_B_hq": shortest_from_b[i] if i < len(shortest_from_b) else None,
            "degree": len(game.map.adjacency[i]) if i < len(game.map.adjacency) else 0,
            "is_stronghold": i in game.map.strongholds,
        }
        for i in range(game.map.n)
    ]

    turn_summary = []
    for snap in replay["snapshots"]:
        turn_summary.append(
            {
                "turn": snap["turn"],
                "A_alive": snap["alive"]["A"],
                "B_alive": snap["alive"]["B"],
                "A_buildings": snap["building_count"]["A"],
                "B_buildings": snap["building_count"]["B"],
                "A_damage_taken": snap["damage_taken"]["A"],
                "B_damage_taken": snap["damage_taken"]["B"],
                "A_siege_done": snap["siege_done"]["A"],
                "B_siege_done": snap["siege_done"]["B"],
            }
        )

    return {
        "command_counts": {s: dict(c) for s, c in command_counts.items()},
        "event_counts": {s: dict(c) for s, c in event_counts.items()},
        "first_turns": first_turns,
        "time_by_turn": time_by_turn,
        "events_rows": events_rows,
        "commands_rows": commands_rows,
        "turn_summary": turn_summary,
        "map_metrics": map_metrics,
        "gold_latest": _latest_by_side(gold_rows, "turn"),
    }


def army_rows(replay: dict) -> list[dict]:
    return [
        {
            "turn": s["turn"],
            "side": side,
            "alive_warriors": s["alive"][side],
            "trained_warriors": s["trained"][side],
            "lost_warriors": s["losses"][side],
        }
        for s in replay["snapshots"]
        for side in ("A", "B")
    ]


def building_rows(replay: dict) -> list[dict]:
    return [
        {
            "turn": s["turn"],
            "side": side,
            "building_count": s["building_count"][side],
            "new_buildings": s["building_acquired"][side],
            "upgrades": s["upgrade_count"][side],
        }
        for s in replay["snapshots"]
        for side in ("A", "B")
    ]


def damage_rows(replay: dict) -> list[dict]:
    return [
        {
            "turn": s["turn"],
            "side": side,
            "damage_taken": s["damage_taken"][side],
            "siege_done": s["siege_done"][side],
        }
        for s in replay["snapshots"]
        for side in ("A", "B")
    ]


def unit_path_rows(replay: dict) -> list[dict]:
    rows = []
    for side in ("A", "B"):
        for unit_id, unit in replay["units"][side].items():
            rows.append(
                {
                    "side": side,
                    "unit_id": unit_id,
                    "first_turn": unit.get("first_turn"),
                    "last_turn": unit.get("last_turn"),
                    "active": unit.get("active"),
                    "hp": unit.get("hp"),
                    "path": " ".join(str(v) for v in unit.get("path", [])),
                }
            )
    return rows


def _shortest_distances(adjacency: list[list[int]], start: int) -> list[int | None]:
    dist: list[int | None] = [None] * len(adjacency)
    if start < 0 or start >= len(adjacency):
        return dist
    dist[start] = 0
    q = deque([start])
    while q:
        cur = q.popleft()
        for nxt in adjacency[cur]:
            if 0 <= nxt < len(adjacency) and dist[nxt] is None:
                dist[nxt] = (dist[cur] or 0) + 1
                q.append(nxt)
    return dist


def _latest_by_side(rows: list[dict], turn_key: str) -> dict:
    latest = {}
    for row in rows:
        latest[row["side"]] = row
    return latest


from __future__ import annotations

import re
from pathlib import Path

from . import __version__
from .models import Command, Event, Game, MapData, Turn
from .rules_config import RULE_VERSION


HEADER_RE = re.compile(r'^\[(LEFT|RIGHT)\s+"(.*)"\]$')
SIDE_MAP = {"LEFT": "A", "RIGHT": "B", "A": "A", "B": "B"}


def parse_log(text: str, source_file: str, source_hash: str) -> Game:
    game = Game(
        game_id=source_hash[:16],
        source_file=source_file,
        source_hash=source_hash,
        parser_version=__version__,
        rule_version=RULE_VERSION,
    )
    lines = [line.rstrip("\n") for line in text.splitlines()]
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line:
            i += 1
            continue
        header = HEADER_RE.match(line)
        if header:
            game.metadata[SIDE_MAP[header.group(1)]] = header.group(2)
            i += 1
        elif line == "MAP":
            map_data, i = _parse_map(lines, i + 1, game.warnings)
            game.map = map_data
        elif line.startswith("TURN ") and not line.endswith(" RESULT"):
            turn, i = _parse_turn(lines, i, game.warnings)
            game.turns.append(turn)
        elif line.startswith("RESULT "):
            parts = line.split(maxsplit=2)
            game.result = {
                "outcome": parts[1] if len(parts) > 1 else "",
                "reason": parts[2] if len(parts) > 2 else "",
                "raw": line,
            }
            i += 1
        else:
            _warn(game.warnings, i + 1, "unknown_top_level_line", line)
            i += 1
    return game


def _parse_map(lines: list[str], i: int, warnings: list[dict]) -> tuple[MapData, int]:
    start = i
    try:
        n, k = [int(v) for v in lines[i].split()]
        x = [int(v) for v in lines[i + 1].split()]
        y = [int(v) for v in lines[i + 2].split()]
        strong_parts = lines[i + 3].split()
        strongholds = [int(v) for v in strong_parts[1:]]
        i += 4
        adjacency: list[list[int]] = []
        for _ in range(n):
            parts = [int(v) for v in lines[i].split()]
            adjacency.append(parts[1:])
            i += 1
        if i >= len(lines) or lines[i].strip() != "END MAP":
            _warn(warnings, i + 1, "missing_end_map", lines[i] if i < len(lines) else "")
        else:
            i += 1
        return MapData(n=n, k=k, x=x, y=y, strongholds=strongholds, adjacency=adjacency), i
    except Exception as exc:
        _warn(warnings, start + 1, "map_parse_error", str(exc))
        while i < len(lines) and lines[i].strip() != "END MAP":
            i += 1
        return MapData(), min(i + 1, len(lines))


def _parse_turn(lines: list[str], i: int, warnings: list[dict]) -> tuple[Turn, int]:
    raw = lines[i].strip()
    try:
        turn_no = int(raw.split()[1])
    except Exception:
        turn_no = -1
        _warn(warnings, i + 1, "bad_turn_header", raw)
    turn = Turn(turn_no=turn_no)
    i += 1
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("COMMAND ") and line.endswith(" START"):
            side_name = line.split()[1]
            side = SIDE_MAP.get(side_name, side_name)
            i += 1
            while i < len(lines) and lines[i].strip() != f"COMMAND {side_name} END":
                command_line = lines[i].strip()
                if command_line:
                    command = _parse_command(turn_no, side, command_line)
                    if command:
                        turn.commands.setdefault(side, []).append(command)
                    else:
                        _warn(warnings, i + 1, "unknown_command", command_line)
                i += 1
            i += 1
        elif line == f"TURN {turn_no} RESULT":
            i += 1
            while i < len(lines) and lines[i].strip() != f"END TURN {turn_no}":
                event_line = lines[i].strip()
                if event_line:
                    events = _parse_event(turn_no, event_line)
                    if events:
                        turn.events.extend(events)
                    else:
                        _warn(warnings, i + 1, "unknown_event", event_line)
                i += 1
            if i < len(lines):
                i += 1
            return turn, i
        elif line.startswith("END TURN "):
            i += 1
            return turn, i
        else:
            _warn(warnings, i + 1, "unexpected_turn_line", line)
            i += 1
    return turn, i


def _parse_command(turn_no: int, side: str, raw: str) -> Command | None:
    parts = raw.split()
    if not parts:
        return None
    typ = parts[0]
    if typ == "MOVE" and len(parts) == 3:
        return Command(turn_no, side, "MOVE", parts[1], int(parts[2]), raw)
    if typ == "TRAIN" and len(parts) == 2:
        return Command(turn_no, side, "TRAIN", None, int(parts[1]), raw)
    if typ == "UPGRADE" and len(parts) == 2:
        return Command(turn_no, side, "UPGRADE", None, int(parts[1]), raw)
    return None


def _parse_event(turn_no: int, raw: str) -> list[Event]:
    parts = raw.split()
    if not parts:
        return []
    typ = parts[0]
    if typ == "TIME" and len(parts) >= 7:
        return [
            Event(turn_no, "TIME", raw, side="A", time_used_ms=int(parts[2]), time_tokens=int(parts[3])),
            Event(turn_no, "TIME", raw, side="B", time_used_ms=int(parts[5]), time_tokens=int(parts[6])),
        ]
    if typ == "UPGRADE" and len(parts) == 3:
        return [Event(turn_no, "UPGRADE", raw, side=SIDE_MAP.get(parts[1]), location=int(parts[2]))]
    if typ == "TRAIN" and len(parts) >= 2:
        return [
            Event(turn_no, "TRAIN", raw, side=unit_side(uid), unit_id=uid, amount=1)
            for uid in parts[1:]
            if unit_side(uid)
        ]
    if typ == "MOVE" and len(parts) == 3:
        return [Event(turn_no, "MOVE", raw, side=unit_side(parts[1]), unit_id=parts[1], location=int(parts[2]))]
    if typ == "DAMAGE" and len(parts) == 4:
        return [
            Event(
                turn_no,
                "DAMAGE",
                raw,
                side=unit_side(parts[2]),
                unit_id=parts[2],
                amount=int(parts[3]),
                cause=parts[1],
            )
        ]
    if typ == "SIEGE" and len(parts) == 4:
        return [Event(turn_no, "SIEGE", raw, side=SIDE_MAP.get(parts[1]), location=int(parts[2]), amount=int(parts[3]))]
    return []


def unit_side(unit_id: str) -> str | None:
    if unit_id.startswith("A"):
        return "A"
    if unit_id.startswith("B"):
        return "B"
    return None


def _warn(warnings: list[dict], line_no: int, code: str, raw: str) -> None:
    warnings.append({"line": line_no, "code": code, "raw": raw})


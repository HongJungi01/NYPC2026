from __future__ import annotations

from .models import Game


def validate_game(game: Game) -> dict:
    issues: list[dict] = []
    m = game.map
    if not m.n or len(m.x) != m.n or len(m.y) != m.n:
        issues.append({"code": "bad_coordinate_length", "detail": f"N={m.n}, x={len(m.x)}, y={len(m.y)}"})
    if m.k != len(m.strongholds):
        issues.append({"code": "bad_stronghold_count", "detail": f"K={m.k}, strongholds={len(m.strongholds)}"})
    if len(m.adjacency) != m.n:
        issues.append({"code": "bad_adjacency_length", "detail": f"N={m.n}, adjacency={len(m.adjacency)}"})
    turn_numbers = [t.turn_no for t in game.turns]
    if len(turn_numbers) != len(set(turn_numbers)):
        issues.append({"code": "duplicate_turn", "detail": "turn numbers are not unique"})
    if not game.result:
        issues.append({"code": "missing_result", "detail": "no final RESULT line"})

    status = "VALID"
    if not m.n or not game.turns:
        status = "UNSUPPORTED_FORMAT"
    elif issues:
        status = "INVALID"
    elif game.warnings:
        status = "VALID_WITH_WARNINGS"

    reason = " ".join(str(v) for v in game.result.values()).upper()
    wa_terminated = "WA" in reason
    if wa_terminated and status in {"VALID", "VALID_WITH_WARNINGS"}:
        status = "WA_TERMINATED"

    game.validation_status = status
    return {"status": status, "issues": issues, "wa_terminated": wa_terminated}


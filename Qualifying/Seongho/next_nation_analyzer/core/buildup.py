from __future__ import annotations


def build_buildup_timeline(game, replay: dict, diagnosis: dict) -> list[dict]:
    rows = []
    winner = diagnosis.get("winner") or diagnosis.get("missed_win_side")
    for turn in game.turns:
        for side, commands in turn.commands.items():
            for command in commands:
                if command.type in {"MOVE", "UPGRADE", "TRAIN"}:
                    rows.append(
                        {
                            "turn": turn.turn_no,
                            "side": side,
                            "action_type": command.type,
                            "description": f"{side} side {command.raw}",
                            "metric_delta": "",
                            "linked_main_factor": diagnosis.get("main_factor"),
                            "confidence": 0.62 if side == winner else 0.48,
                        }
                    )
        if len(rows) >= 20:
            break
    decisive = diagnosis.get("turning_points", [{}])[0].get("turn") if diagnosis.get("turning_points") else None
    if decisive:
        for snap in replay["snapshots"]:
            if snap["turn"] == decisive:
                rows.append(
                    {
                        "turn": decisive,
                        "side": winner or "",
                        "action_type": "CRITICAL_TURN",
                        "description": f"병력/건물 격차가 크게 벌어진 후보 턴",
                        "metric_delta": str(snap),
                        "linked_main_factor": diagnosis.get("main_factor"),
                        "confidence": diagnosis.get("confidence", 0.5),
                    }
                )
                break
    return sorted(rows, key=lambda r: r["turn"])

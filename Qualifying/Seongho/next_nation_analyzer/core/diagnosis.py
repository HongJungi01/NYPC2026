from __future__ import annotations


def diagnose(game, replay: dict, metrics: dict, gold_rows: list[dict]) -> dict:
    result_type = _result_type(game.result)
    winner = _winner(game.result)
    loser = "B" if winner == "A" else "A" if winner == "B" else None
    final_snap = replay["snapshots"][-1] if replay["snapshots"] else None
    missed_win_side = None
    draw_analysis = {}
    if result_type == "DRAW":
        missed_win_side = _advantage_side(replay, gold_rows)
        draw_analysis = _draw_analysis(game, replay, metrics, gold_rows, missed_win_side)
    scores = []
    if winner and final_snap:
        scores.append(_score_gap("ARMY_GAP", final_snap["alive"][winner] - final_snap["alive"][loser], 2.0, "병력 수 격차"))
        scores.append(
            _score_gap(
                "BUILDING_CONTROL",
                final_snap["building_count"][winner] - final_snap["building_count"][loser],
                1.5,
                "건물 수 격차",
            )
        )
        winner_gold = _last_gold(gold_rows, winner)
        loser_gold = _last_gold(gold_rows, loser)
        scores.append(_score_gap("ECONOMY_TO_ARMY_SNOWBALL", winner_gold - loser_gold, 150.0, "추정 골드 잔액 격차"))
    hunger = _first_event_turn(game, "DAMAGE", cause="HUNGER")
    if hunger:
        side = hunger[1]
        scores.append({"factor": "HUNGER_COLLAPSE", "score": 0.9 if side == loser else 0.4, "evidence": f"{side} side 첫 HUNGER 피해 {hunger[0]}턴"})
    first_siege = {s: metrics["first_turns"][s]["SIEGE"] for s in ("A", "B")}
    if winner and first_siege[winner] and (not first_siege[loser] or first_siege[winner] + 10 < first_siege[loser]):
        scores.append({"factor": "SIEGE_TIMING", "score": 0.75, "evidence": f"{winner} side가 공성을 더 빨리 시작"})

    best = max(scores, key=lambda x: x["score"], default={"factor": "LOW_CONFIDENCE_MIXED_FACTORS", "score": 0.35, "evidence": "뚜렷한 단일 격차가 약함"})
    if result_type == "DRAW" and draw_analysis:
        best = {
            "factor": draw_analysis["main_factor"],
            "score": draw_analysis["confidence"],
            "evidence": draw_analysis["primary_evidence"],
        }
    confidence = max(0.2, min(0.95, best["score"]))
    decisive_turn = _turn_of_max_gap(replay, winner, loser) if winner and loser else None
    if result_type == "DRAW":
        decisive_turn = draw_analysis.get("missed_opportunity_turn")
    loss_reasons = _loss_reasons(game, metrics, winner, loser)
    diagnosis = {
        "result_type": result_type,
        "side": loser,
        "winner": winner,
        "missed_win_side": missed_win_side,
        "main_factor": best["factor"],
        "issue_code": best["factor"],
        "summary": _summary(best["factor"], winner, loser, best["evidence"], result_type, missed_win_side),
        "decisive_turn_range": _range_around(decisive_turn),
        "confidence": round(confidence, 2),
        "evidence": [best["evidence"]],
        "counter_evidence": [],
        "assumption": ["골드와 HP는 로그 이벤트 기반 추정값입니다."],
        "needs_review": confidence < 0.5 or game.validation_status == "WA_TERMINATED",
        "turning_points": [{"turn": decisive_turn, "issue": best["factor"], "confidence": round(confidence, 2)}] if decisive_turn else [],
        "loss_reasons": loss_reasons,
        "recommended_action": _recommend(best["factor"]),
        "target_module": _target_module(best["factor"]),
        "priority": "high" if confidence >= 0.7 else "medium",
        "draw_analysis": draw_analysis,
        "alternative_actions": _alternative_actions(best["factor"], result_type),
        "unfavorable_phase": _range_around(decisive_turn),
    }
    return diagnosis


def _result_type(result: dict) -> str:
    raw = " ".join(str(v) for v in result.values()).upper()
    if "DRAW" in raw or "TIE" in raw:
        return "DRAW"
    if "LEFT_WIN" in raw or "RIGHT_WIN" in raw or raw.startswith("A") or raw.startswith("B"):
        return "WIN"
    if "TURN_LIMIT" in raw and "WIN" not in raw:
        return "DRAW"
    return "UNKNOWN"


def _winner(result: dict) -> str | None:
    raw = " ".join(str(v) for v in result.values()).upper()
    if "DRAW" in raw or "TIE" in raw:
        return None
    if "LEFT" in raw or " A" in raw or raw.startswith("A"):
        return "A"
    if "RIGHT" in raw or " B" in raw or raw.startswith("B"):
        return "B"
    return None


def _score_gap(factor: str, gap: float, threshold: float, label: str) -> dict:
    score = min(0.9, max(0.25, gap / threshold * 0.35 + 0.45))
    return {"factor": factor, "score": score, "evidence": f"{label}: {gap:.1f}"}


def _last_gold(rows: list[dict], side: str) -> float:
    values = [r["max_estimated_gold"] for r in rows if r["side"] == side]
    return float(values[-1]) if values else 0.0


def _first_event_turn(game, typ: str, cause: str | None = None):
    for turn in game.turns:
        for event in turn.events:
            if event.type == typ and (cause is None or event.cause == cause):
                return turn.turn_no, event.side
    return None


def _turn_of_max_gap(replay: dict, winner: str, loser: str) -> int | None:
    best_turn = None
    best_gap = -10**9
    for snap in replay["snapshots"]:
        gap = (snap["alive"][winner] - snap["alive"][loser]) + (snap["building_count"][winner] - snap["building_count"][loser]) * 2
        if gap > best_gap:
            best_gap = gap
            best_turn = snap["turn"]
    return best_turn


def _range_around(turn: int | None) -> str | None:
    if turn is None:
        return None
    return f"{max(1, turn - 5)}-{turn + 5}"


def _summary(factor: str, winner: str | None, loser: str | None, evidence: str, result_type: str, missed_win_side: str | None) -> str:
    if result_type == "DRAW":
        if missed_win_side:
            return f"무승부지만 {missed_win_side} side가 승리로 전환할 여지가 있었습니다. 놓친 핵심 요인은 {factor}입니다. 근거: {evidence}."
        return f"무승부 경기입니다. 승리 전환 실패 요인은 {factor} 후보로 추정됩니다. 근거: {evidence}."
    if not winner:
        return f"승자 판정이 불명확해 {factor} 후보만 제시합니다. 근거: {evidence}."
    return f"{winner} side가 {loser} side 대비 {factor} 흐름에서 앞섰습니다. 근거: {evidence}."


def _loss_reasons(game, metrics: dict, winner: str | None, loser: str | None) -> list[dict]:
    if not winner or not loser:
        return []
    reasons = []
    if metrics["event_counts"][loser].get("TRAIN", 0) + 2 < metrics["event_counts"][winner].get("TRAIN", 0):
        reasons.append({"reason": "LOW_TRAINING_VOLUME", "confidence": 0.68, "evidence": "패배 진영 훈련 이벤트가 더 적음"})
    if metrics["first_turns"][loser].get("SIEGE") is None:
        reasons.append({"reason": "LATE_FIRST_SIEGE", "confidence": 0.72, "evidence": "패배 진영 공성 이벤트 없음"})
    if _first_event_turn(game, "DAMAGE", "HUNGER"):
        reasons.append({"reason": "HUNGER_COLLAPSE", "confidence": 0.7, "evidence": "HUNGER 피해 발생"})
    return reasons


def _recommend(factor: str) -> str:
    return {
        "ECONOMY_TO_ARMY_SNOWBALL": "초반 거점 확보와 훈련 예산 배분 조건을 점검하세요.",
        "ARMY_GAP": "훈련 우선순위와 공격 전환 전 최소 병력 조건을 조정하세요.",
        "BUILDING_CONTROL": "핵심 거점 이동 목적지와 업그레이드 가중치를 점검하세요.",
        "SIEGE_TIMING": "공격 전환 조건과 본부 접근 경로 선택을 낮은 턴부터 평가하세요.",
        "MISSED_SIEGE_CONVERSION": "공성 가능 시점 이후 병력을 본부 방향으로 집중하고 공격 전환 임계값을 낮추세요.",
        "UNCONVERTED_ADVANTAGE": "병력/경제 우위가 생긴 시점에 확장보다 본부 압박을 우선하세요.",
        "LATE_ATTACK_TRANSITION": "중반 이후 우위 판정 시 공격 모드로 전환하는 조건을 더 빨리 발동하세요.",
        "HUNGER_COLLAPSE": "유지비 부족 전 훈련 억제와 노동 가능 전사 배치를 보강하세요.",
    }.get(factor, "리포트의 차트와 타임라인을 기준으로 병목 행동을 검토하세요.")


def _target_module(factor: str) -> str:
    return {
        "ECONOMY_TO_ARMY_SNOWBALL": "economy_decision",
        "ARMY_GAP": "training_decision",
        "BUILDING_CONTROL": "upgrade_decision",
        "SIEGE_TIMING": "attack_decision",
        "MISSED_SIEGE_CONVERSION": "attack_decision",
        "UNCONVERTED_ADVANTAGE": "strategy_decision",
        "LATE_ATTACK_TRANSITION": "attack_decision",
        "HUNGER_COLLAPSE": "maintenance_decision",
    }.get(factor, "strategy_decision")


def _advantage_side(replay: dict, gold_rows: list[dict]) -> str | None:
    if not replay.get("snapshots"):
        return None
    snap = replay["snapshots"][-1]
    scores = {}
    for side in ("A", "B"):
        other = "B" if side == "A" else "A"
        gold_gap = _last_gold(gold_rows, side) - _last_gold(gold_rows, other)
        scores[side] = (
            (snap["alive"][side] - snap["alive"][other]) * 2.0
            + (snap["building_count"][side] - snap["building_count"][other]) * 3.0
            + gold_gap / 400.0
            + (snap["siege_done"][side] - snap["siege_done"][other]) * 0.2
        )
    if abs(scores["A"] - scores["B"]) < 1.0:
        return None
    return "A" if scores["A"] > scores["B"] else "B"


def _draw_analysis(game, replay: dict, metrics: dict, gold_rows: list[dict], missed_side: str | None) -> dict:
    side = missed_side or "A"
    other = "B" if side == "A" else "A"
    final_snap = replay["snapshots"][-1] if replay.get("snapshots") else None
    first_siege = metrics["first_turns"][side].get("SIEGE")
    other_first_siege = metrics["first_turns"][other].get("SIEGE")
    siege_count = metrics["event_counts"][side].get("SIEGE", 0)
    move_count = metrics["event_counts"][side].get("MOVE", 0)
    train_count = metrics["event_counts"][side].get("TRAIN", 0)
    missed_turn = _draw_missed_turn(replay, side, other) or (game.turns[-1].turn_no if game.turns else None)

    if siege_count == 0:
        factor = "LATE_ATTACK_TRANSITION"
        evidence = f"{side} side가 우위 후보였지만 공성 이벤트가 없어 본부 압박으로 전환하지 못함"
        reason = "우위를 공격으로 연결하지 못했습니다."
    elif first_siege and first_siege > max(60, (game.turns[-1].turn_no if game.turns else 200) * 0.55):
        factor = "MISSED_SIEGE_CONVERSION"
        evidence = f"{side} side 첫 공성이 {first_siege}턴으로 늦어 승리 마무리 시간이 부족했음"
        reason = "공성 전환이 늦어 제한 턴 안에 승리를 확정하지 못했습니다."
    else:
        factor = "UNCONVERTED_ADVANTAGE"
        evidence = f"{side} side가 병력/건물/골드 우위를 승리 조건으로 전환하지 못함"
        reason = "전략적 우위가 있었지만 전선 집중이나 본부 압박이 부족했습니다."

    final_evidence = []
    if final_snap:
        final_evidence.append(f"최종 생존 전사 {side}:{final_snap['alive'][side]} / {other}:{final_snap['alive'][other]}")
        final_evidence.append(f"최종 건물 수 {side}:{final_snap['building_count'][side]} / {other}:{final_snap['building_count'][other]}")
    final_evidence.append(f"첫 공성 {side}:{first_siege if first_siege is not None else '없음'} / {other}:{other_first_siege if other_first_siege is not None else '없음'}")
    final_evidence.append(f"이벤트 수 {side} MOVE:{move_count}, TRAIN:{train_count}, SIEGE:{siege_count}")

    return {
        "missed_win_side": missed_side,
        "main_factor": factor,
        "reason_not_converted": reason,
        "missed_opportunity_turn": missed_turn,
        "primary_evidence": evidence,
        "evidence": final_evidence,
        "what_to_do": _alternative_actions(factor, "DRAW"),
        "confidence": 0.72 if missed_side else 0.55,
    }


def _draw_missed_turn(replay: dict, side: str, other: str) -> int | None:
    best_turn = None
    best_score = -10**9
    for snap in replay.get("snapshots", []):
        score = (
            (snap["alive"][side] - snap["alive"][other]) * 2
            + (snap["building_count"][side] - snap["building_count"][other]) * 3
            + (snap["siege_done"][side] - snap["siege_done"][other])
        )
        if score > best_score:
            best_score = score
            best_turn = snap["turn"]
    return best_turn


def _alternative_actions(factor: str, result_type: str) -> list[str]:
    if result_type == "DRAW":
        return {
            "MISSED_SIEGE_CONVERSION": [
                "첫 공성 이후 병력을 분산하지 말고 적 본부 방향으로 집결시킵니다.",
                "턴 제한이 가까워질수록 거점 확장보다 본부 압박 가중치를 높입니다.",
                "공성 가능 병력이 확인되면 방어용 잔류 병력을 최소화하고 공격 전환 조건을 낮춥니다.",
            ],
            "LATE_ATTACK_TRANSITION": [
                "중반 이후 병력/건물 우위가 생기면 공격 모드로 전환하는 임계값을 낮춥니다.",
                "상대 본부까지의 최단 경로에 있는 병력을 우선 이동시킵니다.",
                "훈련과 업그레이드만 반복하지 말고 승리 조건인 본부 파괴를 직접 목표로 삼습니다.",
            ],
            "UNCONVERTED_ADVANTAGE": [
                "우위가 생긴 시점에 병력을 한 방향으로 묶어 전선 돌파를 시도합니다.",
                "피해가 큰 병목 구역을 피하거나, 숫자 우위가 있을 때만 진입합니다.",
                "마지막 25% 턴 구간에서는 골드 축적보다 공격 생산과 공성 이동을 우선합니다.",
            ],
        }.get(factor, ["무승부 직전 구간에서 공격 전환, 병력 집중, 본부 접근 경로를 우선 점검하세요."])
    return {
        "ECONOMY_TO_ARMY_SNOWBALL": [
            "초반 핵심 거점 이동 우선순위를 높입니다.",
            "훈련 비용과 업그레이드 비용이 충돌할 때 병력 공백이 생기지 않도록 조건을 조정합니다.",
        ],
        "ARMY_GAP": [
            "병력 열세가 시작되는 구간에서 훈련 우선순위를 높입니다.",
            "전투 손실이 누적되는 병목 구역에 소수 병력을 반복 투입하지 않습니다.",
        ],
        "BUILDING_CONTROL": [
            "중앙 또는 핵심 거점 건설 타이밍을 앞당깁니다.",
            "상대가 먼저 확보한 거점은 우회하거나 병력 우위가 있을 때 탈환합니다.",
        ],
        "SIEGE_TIMING": [
            "상대보다 공성이 늦으면 공격 전환 조건을 낮춥니다.",
            "본부 접근 경로 위 병력을 분산시키지 않습니다.",
        ],
    }.get(factor, ["분기점 전후 10턴의 명령을 확인해 병력 집중과 공격 전환 조건을 조정하세요."])

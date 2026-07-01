from __future__ import annotations

import csv
import json
import shutil
from pathlib import Path
from typing import Iterable

from . import __version__


def write_outputs(out_dir: Path, bundle: dict) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    raw_dir = out_dir / "raw_data"
    raw_dir.mkdir(exist_ok=True)
    _remove_old_raw_files(out_dir)

    _write_json(raw_dir / "input_manifest.json", bundle["manifest"])
    _write_json(raw_dir / "game_analysis.json", bundle["game_analysis"])
    _write_json(raw_dir / "parse_warnings.json", bundle["game"].warnings)
    _write_json(raw_dir / "main_factor_summary.json", bundle["diagnosis"])
    _write_json(raw_dir / "chart_data.json", bundle["chart_data"])

    _write_csv(raw_dir / "events.csv", bundle["metrics"]["events_rows"])
    _write_csv(raw_dir / "commands.csv", bundle["metrics"]["commands_rows"])
    _write_csv(raw_dir / "turn_summary.csv", bundle["metrics"]["turn_summary"])
    _write_csv(raw_dir / "economy_by_turn.csv", bundle["gold_rows"])
    _write_csv(raw_dir / "gold_by_turn.csv", bundle["gold_rows"])
    _write_csv(raw_dir / "gold_estimation_detail.csv", bundle["gold_details"])
    _write_csv(raw_dir / "army_by_turn.csv", bundle["army_rows"])
    _write_csv(raw_dir / "warriors_by_turn.csv", bundle["army_rows"])
    _write_csv(raw_dir / "buildings_by_turn.csv", bundle["building_rows"])
    _write_csv(raw_dir / "damage_by_turn.csv", bundle["damage_rows"])
    _write_csv(raw_dir / "unit_paths.csv", bundle["unit_path_rows"])
    _write_csv(raw_dir / "combat_zones.csv", bundle["replay"].get("combat_zones", []))
    _write_csv(raw_dir / "map_metrics.csv", bundle["metrics"]["map_metrics"])
    _write_csv(raw_dir / "diagnostics.csv", _diagnostics_rows(bundle["diagnosis"]))
    _write_csv(raw_dir / "buildup_timeline.csv", bundle["buildup"])

    (raw_dir / "validation_report.md").write_text(_validation_md(bundle), encoding="utf-8")
    report = _analysis_md(bundle)
    (raw_dir / "analysis_report.md").write_text(report, encoding="utf-8")
    (out_dir / "human_summary.md").write_text(_human_summary_md(bundle), encoding="utf-8")
    (raw_dir / "single_game_diagnosis.md").write_text(_diagnosis_md(bundle), encoding="utf-8")
    (raw_dir / "strategy_recommendations.md").write_text(_recommendations_md(bundle), encoding="utf-8")


def _remove_old_raw_files(out_dir: Path) -> None:
    raw_names = {
        "input_manifest.json",
        "game_analysis.json",
        "parse_warnings.json",
        "main_factor_summary.json",
        "chart_data.json",
        "events.csv",
        "commands.csv",
        "turn_summary.csv",
        "economy_by_turn.csv",
        "gold_by_turn.csv",
        "gold_estimation_detail.csv",
        "army_by_turn.csv",
        "warriors_by_turn.csv",
        "buildings_by_turn.csv",
        "damage_by_turn.csv",
        "unit_paths.csv",
        "combat_zones.csv",
        "map_metrics.csv",
        "diagnostics.csv",
        "buildup_timeline.csv",
        "validation_report.md",
        "analysis_report.md",
        "single_game_diagnosis.md",
        "strategy_recommendations.md",
        "chart_generation_status.json",
        ".DS_Store",
    }
    for name in raw_names:
        path = out_dir / name
        if path.exists() and path.is_file():
            path.unlink()
    old_mpl_config = out_dir / ".mplconfig"
    if old_mpl_config.exists() and old_mpl_config.is_dir():
        shutil.rmtree(old_mpl_config)
    old_raw_charts = out_dir / "raw_data" / "charts"
    if old_raw_charts.exists() and old_raw_charts.is_dir():
        shutil.rmtree(old_raw_charts)
    old_raw_mpl_config = out_dir / "raw_data" / ".mplconfig"
    if old_raw_mpl_config.exists() and old_raw_mpl_config.is_dir():
        shutil.rmtree(old_raw_mpl_config)


def _write_json(path: Path, data) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def _write_csv(path: Path, rows: Iterable[dict]) -> None:
    rows = list(rows)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields = sorted({key for row in rows for key in row.keys()})
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def _diagnostics_rows(diagnosis: dict) -> list[dict]:
    rows = [
        {
            "side": diagnosis.get("side"),
            "main_factor": diagnosis.get("main_factor"),
            "issue_code": diagnosis.get("issue_code"),
            "confidence": diagnosis.get("confidence"),
            "evidence": " | ".join(diagnosis.get("evidence", [])),
            "recommended_action": diagnosis.get("recommended_action"),
            "target_module": diagnosis.get("target_module"),
            "priority": diagnosis.get("priority"),
        }
    ]
    for reason in diagnosis.get("loss_reasons", []):
        rows.append({"side": diagnosis.get("side"), "issue_code": reason["reason"], **reason})
    return rows


def _validation_md(bundle: dict) -> str:
    validation = bundle["validation"]
    game = bundle["game"]
    return "\n".join(
        [
            "# Validation Report",
            "",
            f"- Analyzer version: `{__version__}`",
            f"- Status: `{validation['status']}`",
            f"- WA terminated: `{validation['wa_terminated']}`",
            f"- Source hash: `{game.source_hash}`",
            f"- Parse warnings: {len(game.warnings)}",
            f"- Validation issues: {len(validation['issues'])}",
            "",
            "## Issues",
            "",
            *[f"- `{i['code']}`: {i['detail']}" for i in validation["issues"]],
        ]
    )


def _analysis_md(bundle: dict) -> str:
    game = bundle["game"]
    d = bundle["diagnosis"]
    metrics = bundle["metrics"]
    charts = [
        "gold_income_by_turn.png",
        "gold_spend_by_turn.png",
        "gold_balance_by_turn.png",
        "alive_warriors_by_turn.png",
        "building_count_by_turn.png",
        "damage_by_turn.png",
        "siege_by_turn.png",
        "map_combat_zones.png",
        "unit_paths.png",
    ]
    lines = [
        "# NEXT NATION Single Game Analysis",
        "",
        "## Summary",
        "",
        f"- Validation: `{game.validation_status}`",
        f"- Winner: `{d.get('winner') or 'unknown'}`",
        f"- End turn: `{game.turns[-1].turn_no if game.turns else 'unknown'}`",
        f"- Result: `{game.result.get('raw', '')}`",
        f"- Main factor: `{d.get('main_factor')}`",
        f"- Confidence: `{d.get('confidence')}`",
        f"- Decisive turn range: `{d.get('decisive_turn_range')}`",
        "",
        d.get("summary", ""),
        "",
        "## A Side vs B Side Counts",
        "",
        f"- Commands: `{metrics['command_counts']}`",
        f"- Events: `{metrics['event_counts']}`",
        f"- First turns: `{metrics['first_turns']}`",
        "",
        "## Buildup Timeline",
        "",
    ]
    for row in bundle["buildup"][:30]:
        lines.append(f"- Turn {row['turn']} `{row['side']}` `{row['action_type']}`: {row['description']}")
    lines.extend(["", "## Loss Reasons", ""])
    for reason in d.get("loss_reasons", []):
        lines.append(f"- `{reason['reason']}` ({reason['confidence']}): {reason['evidence']}")
    lines.extend(["", "## Charts", ""])
    for chart in charts:
        lines.append(f"![{chart}](../charts/{chart})")
    lines.extend(["", "## Assumptions", ""])
    for assumption in d.get("assumption", []):
        lines.append(f"- {assumption}")
    return "\n".join(lines) + "\n"


def _diagnosis_md(bundle: dict) -> str:
    d = bundle["diagnosis"]
    return "\n".join(
        [
            "# Single Game Diagnosis",
            "",
            f"- Main factor: `{d.get('main_factor')}`",
            f"- Losing side: `{d.get('side')}`",
            f"- Confidence: `{d.get('confidence')}`",
            f"- Needs review: `{d.get('needs_review')}`",
            f"- Recommended action: {d.get('recommended_action')}",
            f"- Target module: `{d.get('target_module')}`",
        ]
    )


def _human_summary_md(bundle: dict) -> str:
    game = bundle["game"]
    diagnosis = bundle["diagnosis"]
    metrics = bundle["metrics"]
    validation = bundle["validation"]
    final_army = _last_values_by_side(bundle["army_rows"], "alive_warriors")
    final_buildings = _last_values_by_side(bundle["building_rows"], "building_count")
    final_gold = _last_values_by_side(bundle["gold_rows"], "max_estimated_gold")
    total_trained = _sum_by_side(bundle["army_rows"], "trained_warriors")
    total_losses = _sum_by_side(bundle["army_rows"], "lost_warriors")
    total_damage_taken = _sum_by_side(bundle["damage_rows"], "damage_taken")
    total_siege_done = _sum_by_side(bundle["damage_rows"], "siege_done")
    first = metrics["first_turns"]
    result_type = diagnosis.get("result_type", "UNKNOWN")
    winner = diagnosis.get("winner") or "unknown"
    loser = diagnosis.get("side") or "unknown"
    missed_win_side = diagnosis.get("missed_win_side") or "unknown"
    end_turn = game.turns[-1].turn_no if game.turns else "unknown"
    turning = diagnosis.get("turning_points", [])
    turning_turn = turning[0].get("turn") if turning else None
    phase_rows = _phase_summary(bundle)
    top_damage_turns = _top_turns(bundle["damage_rows"], "damage_taken", limit=8)
    top_siege_turns = _top_turns(bundle["damage_rows"], "siege_done", limit=8)
    top_combat_zones = _top_regions(bundle["replay"].get("combat_zones", []), limit=10)
    top_siege_zones = _top_event_regions(metrics["events_rows"], "SIEGE", limit=10)
    train_commands = metrics["command_counts"]
    event_counts = metrics["event_counts"]
    chart_status = "`raw_data/chart_generation_status.json`에서 확인"

    lines = [
        "# 사람이 읽기 쉬운 경기 요약",
        "",
        "> 이 파일은 CSV/JSON을 직접 열지 않고도 경기 흐름을 판단할 수 있도록 자동으로 요약한 리포트입니다.",
        "> 원본에 가까운 자세한 데이터는 `raw_data/` 폴더에 따로 보관됩니다.",
        "",
        "## 한 줄 결론",
        "",
        _outcome_sentence(result_type, winner, loser, missed_win_side, diagnosis),
        f"- 신뢰도는 `{diagnosis.get('confidence')}`이며, 결정적 분기 후보는 `{diagnosis.get('decisive_turn_range')}`입니다.",
        f"- 자동 해석: {diagnosis.get('summary')}",
        "",
        "## 경기 기본 정보",
        "",
        f"- 결과: `{game.result.get('raw', '')}`",
        f"- 종료 턴: `{end_turn}`",
        f"- 검증 상태: `{game.validation_status}`",
        f"- WA 종료 여부: `{validation.get('wa_terminated')}`",
        f"- 파싱 경고 수: `{len(game.warnings)}`",
        f"- 맵 구역 수 N: `{game.map.n}`",
        f"- 핵심 거점 수 K: `{game.map.k}`",
        f"- 결과 유형: `{result_type}`",
        f"- 승리 진영: `{winner}`",
        f"- 패배 진영: `{loser}`",
        f"- 무승부 시 승리 기회를 놓친 후보 진영: `{missed_win_side}`",
        "",
        "## 최종 격차",
        "",
        "| 지표 | A side | B side | 차이(A-B) |",
        "|---|---:|---:|---:|",
        _table_row("생존 전사 수", final_army),
        _table_row("건물 수", final_buildings),
        _table_row("추정 골드 잔액", final_gold),
        _table_row("총 훈련 수", total_trained),
        _table_row("총 손실 수", total_losses),
        _table_row("받은 피해량", total_damage_taken),
        _table_row("공성량", total_siege_done),
        "",
        "### 최종 격차 해석",
        "",
        _gap_sentence("생존 전사", final_army, "명"),
        _gap_sentence("건물", final_buildings, "개"),
        _gap_sentence("추정 골드 잔액", final_gold, "골드"),
        _gap_sentence("총 훈련 수", total_trained, "명"),
        _gap_sentence("총 전사 손실", total_losses, "명"),
        _gap_sentence("받은 피해량", total_damage_taken, "피해"),
        _gap_sentence("공성량", total_siege_done, "피해"),
        "",
        "## 구간별 흐름",
        "",
        "| 구간 | 턴 범위 | A 생존 | B 생존 | A 건물 | B 건물 | A 추정 골드 | B 추정 골드 | 해석 |",
        "|---|---|---:|---:|---:|---:|---:|---:|---|",
        *phase_rows,
        "",
        "## 주요 타이밍",
        "",
        "| 항목 | A side | B side |",
        "|---|---:|---:|",
        _first_turn_row("첫 훈련", first, "TRAIN"),
        _first_turn_row("첫 이동", first, "MOVE"),
        _first_turn_row("첫 업그레이드", first, "UPGRADE"),
        _first_turn_row("첫 전투 피해", first, "COMBAT"),
        _first_turn_row("첫 공성", first, "SIEGE"),
        "",
        "### 타이밍 해석",
        "",
        _timing_sentence("첫 훈련", first, "TRAIN"),
        _timing_sentence("첫 업그레이드", first, "UPGRADE"),
        _timing_sentence("첫 전투 피해", first, "COMBAT"),
        _timing_sentence("첫 공성", first, "SIEGE"),
        "",
        "## 명령과 실제 이벤트 규모",
        "",
        "| 항목 | A side | B side | 차이(A-B) |",
        "|---|---:|---:|---:|",
        _count_row("MOVE 명령", train_commands, "MOVE"),
        _count_row("TRAIN 명령", train_commands, "TRAIN"),
        _count_row("UPGRADE 명령", train_commands, "UPGRADE"),
        _count_row("MOVE 이벤트", event_counts, "MOVE"),
        _count_row("TRAIN 이벤트", event_counts, "TRAIN"),
        _count_row("UPGRADE 이벤트", event_counts, "UPGRADE"),
        _count_row("DAMAGE 이벤트", event_counts, "DAMAGE"),
        _count_row("SIEGE 이벤트", event_counts, "SIEGE"),
        "",
        "## 피해가 컸던 턴",
        "",
        "### 전사 피해 기준",
        "",
        *(_ranked_turn_lines(top_damage_turns, "피해") or ["- 기록된 전사 피해가 없습니다."]),
        "",
        "### 공성 피해 기준",
        "",
        *(_ranked_turn_lines(top_siege_turns, "공성") or ["- 기록된 공성 피해가 없습니다."]),
        "",
        "## 전투/공성 집중 구역",
        "",
        "### 전사 피해가 많이 발생한 구역",
        "",
        *(_ranked_region_lines(top_combat_zones) or ["- 위치를 추정할 수 있는 전사 피해 구역이 없습니다."]),
        "",
        "### 공성이 많이 발생한 구역",
        "",
        *(_ranked_region_lines(top_siege_zones) or ["- 공성 구역 기록이 없습니다."]),
        "",
        "## 해석",
        "",
        f"- 메인 요인: `{diagnosis.get('main_factor')}`",
        f"- 이슈 코드: `{diagnosis.get('issue_code')}`",
        f"- 근거: {' / '.join(diagnosis.get('evidence', []))}",
        f"- 사람이 확인할 필요: `{diagnosis.get('needs_review')}`",
    ]
    if turning_turn:
        lines.append(f"- 가장 중요한 분기 후보 턴: `{turning_turn}`")
    lines.extend(_result_specific_analysis(result_type, diagnosis))
    lines.extend(["", "## 자동 진단 상세", ""])
    if result_type == "DRAW":
        lines.extend(
            [
                f"- 분석 관점: `무승부를 승리 기회를 놓친 경기로 분석`",
                f"- 승리 전환 후보 진영: `{diagnosis.get('missed_win_side')}`",
                f"- 메인 요인: `{diagnosis.get('main_factor')}`",
                f"- 신뢰도: `{diagnosis.get('confidence')}`",
                f"- 우선순위: `{diagnosis.get('priority')}`",
                f"- 점검 대상 모듈: `{diagnosis.get('target_module')}`",
                f"- 추천 액션: {diagnosis.get('recommended_action')}",
                f"- 사람이 재검토해야 하는가: `{diagnosis.get('needs_review')}`",
            ]
        )
    else:
        lines.extend(
            [
                f"- 진단 대상 패배 진영: `{diagnosis.get('side')}`",
                f"- 승리 진영: `{diagnosis.get('winner')}`",
                f"- 메인 요인: `{diagnosis.get('main_factor')}`",
                f"- 신뢰도: `{diagnosis.get('confidence')}`",
                f"- 우선순위: `{diagnosis.get('priority')}`",
                f"- 점검 대상 모듈: `{diagnosis.get('target_module')}`",
                f"- 추천 액션: {diagnosis.get('recommended_action')}",
                f"- 사람이 재검토해야 하는가: `{diagnosis.get('needs_review')}`",
            ]
        )
    if diagnosis.get("evidence"):
        lines.extend(["", "### 진단 근거", ""])
        for evidence in diagnosis.get("evidence", []):
            lines.append(f"- {evidence}")
    if diagnosis.get("counter_evidence"):
        lines.extend(["", "### 반대 근거", ""])
        for evidence in diagnosis.get("counter_evidence", []):
            lines.append(f"- {evidence}")
    if diagnosis.get("assumption"):
        lines.extend(["", "### 분석 가정", ""])
        for assumption in diagnosis.get("assumption", []):
            lines.append(f"- {assumption}")
    if diagnosis.get("turning_points"):
        lines.extend(["", "### 치명적 분기점 후보", ""])
        for point in diagnosis.get("turning_points", []):
            lines.append(
                f"- {point.get('turn')}턴: `{point.get('issue')}` "
                f"(신뢰도 {point.get('confidence')})"
            )
    lines.extend(["", "## 빌드업 행동 타임라인", ""])
    if bundle["buildup"]:
        for row in bundle["buildup"][:40]:
            lines.append(
                f"- {row['turn']}턴 / {row['side']} side / `{row['action_type']}`: "
                f"{row['description']} (연결 요인: `{row['linked_main_factor']}`, 신뢰도 {row['confidence']})"
            )
    else:
        lines.append("- 자동으로 추출된 빌드업 행동이 없습니다.")
    lines.extend(["", "## 무승부 원인 후보" if result_type == "DRAW" else "## 패배 원인 후보", ""])
    reasons = diagnosis.get("loss_reasons", [])
    if result_type == "DRAW":
        draw = diagnosis.get("draw_analysis") or {}
        lines.append(f"- `{draw.get('main_factor') or diagnosis.get('main_factor')}`: {draw.get('reason_not_converted') or diagnosis.get('summary')} (신뢰도 {diagnosis.get('confidence')})")
    elif reasons:
        for reason in reasons:
            lines.append(f"- `{reason['reason']}`: {reason['evidence']} (신뢰도 {reason['confidence']})")
    else:
        lines.append("- 뚜렷한 보조 패배 원인은 낮은 신뢰도로만 감지되었습니다.")
    lines.extend(
        [
            "",
            "## 먼저 열어볼 차트",
            "",
            "1. `charts/alive_warriors_by_turn.png` - 병력 격차가 언제 벌어졌는지 확인",
            "2. `charts/gold_balance_by_turn.png` - 추정 골드 흐름 확인",
            "3. `charts/building_count_by_turn.png` - 건물/거점 격차 확인",
            "4. `charts/damage_by_turn.png` - 피해가 집중된 턴 확인",
            "5. `charts/unit_paths.png` - 병력 이동 경로 확인",
            "",
            f"- 차트 생성 상태: {chart_status}",
            "",
            "## 폴더 구조",
            "",
            "- `human_summary.md`: 사람이 먼저 읽을 유일한 메인 리포트",
            "- `raw_data/`: 사람이 직접 판단하기 힘든 모든 원자료와 보조 파일 보관",
            "- `charts/`: PNG 그래프",
            "- `raw_data/analysis_report.md`: 기존 자동 분석 리포트 백업",
            "- `raw_data/single_game_diagnosis.md`: 기존 단일 진단 파일 백업",
            "- `raw_data/strategy_recommendations.md`: 기존 전략 추천 파일 백업",
            "",
            "## 전략 추천 통합",
            "",
            f"- {diagnosis.get('recommended_action')}",
            f"- 점검 대상 모듈: `{diagnosis.get('target_module')}`",
            f"- 우선순위: `{diagnosis.get('priority')}`",
            *[f"- 대안 후보: {action}" for action in diagnosis.get("alternative_actions", [])],
            "- `charts/alive_warriors_by_turn.png`에서 병력 격차가 벌어지는 시점을 먼저 확인하세요.",
            "- 분기점 전후 10턴의 명령은 `raw_data/commands.csv`에서 확인할 수 있습니다.",
            "- 실제 피해가 집중된 구역은 `raw_data/combat_zones.csv`와 `charts/map_combat_zones.png`를 같이 보세요.",
            "",
            "## 주의",
            "",
            "- 골드와 HP는 로그에 직접 기록되지 않아 추정값입니다.",
            "- 이 요약은 자동 진단 결과이므로, 결정적 턴 주변의 원본 명령과 차트를 함께 확인하는 것이 좋습니다.",
        ]
    )
    return "\n".join(lines) + "\n"


def _last_values_by_side(rows: list[dict], key: str) -> dict[str, float]:
    values = {"A": 0.0, "B": 0.0}
    for row in rows:
        values[row["side"]] = float(row.get(key, 0) or 0)
    return values


def _sum_by_side(rows: list[dict], key: str) -> dict[str, float]:
    values = {"A": 0.0, "B": 0.0}
    for row in rows:
        values[row["side"]] += float(row.get(key, 0) or 0)
    return values


def _table_row(label: str, values: dict[str, float]) -> str:
    a = values.get("A", 0)
    b = values.get("B", 0)
    return f"| {label} | {a:.0f} | {b:.0f} | {a - b:+.0f} |"


def _first_turn_row(label: str, first: dict, key: str) -> str:
    a = first.get("A", {}).get(key)
    b = first.get("B", {}).get(key)
    return f"| {label} | {a if a is not None else '-'} | {b if b is not None else '-'} |"


def _outcome_sentence(result_type: str, winner: str, loser: str, missed_win_side: str, diagnosis: dict) -> str:
    if result_type == "DRAW":
        if missed_win_side != "unknown":
            return (
                f"- 이 경기는 무승부입니다. 단순히 비긴 경기가 아니라, "
                f"{missed_win_side} side가 `{diagnosis.get('main_factor')}` 문제로 승리 전환 기회를 놓친 경기로 분석합니다."
            )
        return f"- 이 경기는 무승부입니다. 승리 전환 실패의 핵심 후보는 `{diagnosis.get('main_factor')}`입니다."
    if result_type == "WIN":
        return f"- {winner} side가 승리했고 {loser} side가 패배했습니다. 핵심 원인은 `{diagnosis.get('main_factor')}`로 판정했습니다."
    return f"- 결과 유형이 불명확합니다. 핵심 후보 요인은 `{diagnosis.get('main_factor')}`입니다."


def _result_specific_analysis(result_type: str, diagnosis: dict) -> list[str]:
    if result_type == "DRAW":
        draw = diagnosis.get("draw_analysis") or {}
        lines = [
            "",
            "## 무승부 분석: 승리할 수 있었지만 놓친 지점",
            "",
            f"- 승리 전환 후보 진영: `{draw.get('missed_win_side') or diagnosis.get('missed_win_side') or 'unknown'}`",
            f"- 승리로 연결되지 못한 핵심 이유: `{draw.get('main_factor') or diagnosis.get('main_factor')}`",
            f"- 설명: {draw.get('reason_not_converted') or diagnosis.get('summary')}",
            f"- 승리 기회를 놓친 후보 턴: `{draw.get('missed_opportunity_turn') or 'unknown'}`",
            f"- 주요 근거: {draw.get('primary_evidence') or '명확한 단일 근거가 약합니다.'}",
            "",
            "### 왜 승리로 연결되지 못했는가",
            "",
        ]
        for evidence in draw.get("evidence", []):
            lines.append(f"- {evidence}")
        if not draw.get("evidence"):
            lines.append("- 최종 우위 또는 공성 전환 근거가 약해 승리 조건까지 밀어붙이지 못한 것으로 추정됩니다.")
        lines.extend(["", "### 어떤 판단이었다면 승리로 전환할 수 있었나", ""])
        for action in draw.get("what_to_do") or diagnosis.get("alternative_actions", []):
            lines.append(f"- {action}")
        lines.extend(
            [
                "",
                "### 사람이 확인할 턴",
                "",
                "- `raw_data/commands.csv`에서 승리 기회를 놓친 후보 턴 전후 10턴의 MOVE/TRAIN/UPGRADE 명령을 확인하세요.",
                "- `charts/unit_paths.png`에서 공격 병력이 본부 방향으로 모였는지, 중간 거점에서 멈췄는지 확인하세요.",
                "- `charts/siege_by_turn.png`에서 공성량이 늦게 나오거나 끊긴 구간을 확인하세요.",
            ]
        )
        return lines

    if result_type == "WIN":
        lines = [
            "",
            "## 패배 분석: 불리해진 국면과 대안",
            "",
            f"- 패배 진영: `{diagnosis.get('side')}`",
            f"- 불리해진 국면 후보: `{diagnosis.get('unfavorable_phase')}`",
            f"- 패배로 이어진 핵심 판단/수 선택 후보: `{diagnosis.get('main_factor')}`",
            f"- 설명: {diagnosis.get('summary')}",
            "",
            "### 어떤 국면에서 불리해졌는가",
            "",
        ]
        for point in diagnosis.get("turning_points", []):
            lines.append(f"- {point.get('turn')}턴 전후: `{point.get('issue')}` 흐름이 결과와 강하게 연결됩니다.")
        if not diagnosis.get("turning_points"):
            lines.append("- 명확한 단일 분기점은 약하지만, 구간별 병력/건물/골드 격차를 확인해야 합니다.")
        lines.extend(["", "### 어떤 대안이었다면 승리 가능성이 있었나", ""])
        for action in diagnosis.get("alternative_actions", []):
            lines.append(f"- {action}")
        if not diagnosis.get("alternative_actions"):
            lines.append("- 분기점 전후 10턴의 명령을 검토해 공격 전환, 훈련, 거점 확보 조건을 조정하세요.")
        return lines

    return [
        "",
        "## 결과별 추가 분석",
        "",
        "- 결과 유형이 불명확하여 패배/무승부 전용 분석은 낮은 신뢰도로만 제공됩니다.",
        f"- 핵심 후보: `{diagnosis.get('main_factor')}`",
    ]


def _phase_summary(bundle: dict) -> list[str]:
    turns = sorted({row["turn"] for row in bundle["army_rows"]})
    if not turns:
        return []
    max_turn = max(turns)
    checkpoints = sorted({max(1, max_turn // 4), max(1, max_turn // 2), max(1, max_turn * 3 // 4), max_turn})
    rows = []
    start_turn = 1
    for checkpoint in checkpoints:
        army = _values_at_turn(bundle["army_rows"], checkpoint, "alive_warriors")
        buildings = _values_at_turn(bundle["building_rows"], checkpoint, "building_count")
        gold = _values_at_turn(bundle["gold_rows"], checkpoint, "max_estimated_gold")
        interpretation = _phase_interpretation(army, buildings, gold)
        rows.append(
            f"| {len(rows) + 1}구간 | {start_turn}-{checkpoint} | "
            f"{army['A']:.0f} | {army['B']:.0f} | {buildings['A']:.0f} | {buildings['B']:.0f} | "
            f"{gold['A']:.0f} | {gold['B']:.0f} | {interpretation} |"
        )
        start_turn = checkpoint + 1
    return rows


def _values_at_turn(rows: list[dict], turn: int, key: str) -> dict[str, float]:
    values = {"A": 0.0, "B": 0.0}
    best_turn = {"A": -1, "B": -1}
    for row in rows:
        side = row["side"]
        row_turn = int(row["turn"])
        if row_turn <= turn and row_turn >= best_turn[side]:
            values[side] = float(row.get(key, 0) or 0)
            best_turn[side] = row_turn
    return values


def _phase_interpretation(army: dict[str, float], buildings: dict[str, float], gold: dict[str, float]) -> str:
    parts = []
    for label, values, unit in (("병력", army, "명"), ("건물", buildings, "개"), ("골드", gold, "")):
        diff = values["A"] - values["B"]
        if abs(diff) < 1:
            parts.append(f"{label} 비슷함")
        elif diff > 0:
            parts.append(f"A {label} 우위(+{diff:.0f}{unit})")
        else:
            parts.append(f"B {label} 우위({diff:.0f}{unit})")
    return ", ".join(parts)


def _gap_sentence(label: str, values: dict[str, float], unit: str) -> str:
    diff = values.get("A", 0) - values.get("B", 0)
    if abs(diff) < 1:
        return f"- {label}: A와 B가 거의 같습니다."
    leader = "A" if diff > 0 else "B"
    return f"- {label}: {leader} side가 {abs(diff):.0f}{unit} 앞섭니다."


def _timing_sentence(label: str, first: dict, key: str) -> str:
    a = first.get("A", {}).get(key)
    b = first.get("B", {}).get(key)
    if a is None and b is None:
        return f"- {label}: 양쪽 모두 기록이 없습니다."
    if a is None:
        return f"- {label}: B side만 {b}턴에 기록되었습니다."
    if b is None:
        return f"- {label}: A side만 {a}턴에 기록되었습니다."
    if a == b:
        return f"- {label}: 양쪽 모두 {a}턴으로 같습니다."
    leader = "A" if a < b else "B"
    return f"- {label}: {leader} side가 {abs(a - b)}턴 빠릅니다. (A {a}턴, B {b}턴)"


def _count_row(label: str, counts: dict, key: str) -> str:
    a = int(counts.get("A", {}).get(key, 0))
    b = int(counts.get("B", {}).get(key, 0))
    return f"| {label} | {a} | {b} | {a - b:+d} |"


def _top_turns(rows: list[dict], key: str, limit: int) -> list[dict]:
    totals: dict[tuple[int, str], float] = {}
    for row in rows:
        value = float(row.get(key, 0) or 0)
        if value <= 0:
            continue
        pair = (int(row["turn"]), row["side"])
        totals[pair] = totals.get(pair, 0.0) + value
    ranked = sorted(totals.items(), key=lambda item: item[1], reverse=True)[:limit]
    return [{"turn": turn, "side": side, "amount": amount} for (turn, side), amount in ranked]


def _ranked_turn_lines(rows: list[dict], label: str) -> list[str]:
    return [f"- {idx}위: {row['turn']}턴 / {row['side']} side / {row['amount']:.0f} {label}" for idx, row in enumerate(rows, 1)]


def _top_regions(rows: list[dict], limit: int) -> list[dict]:
    totals: dict[int, float] = {}
    for row in rows:
        location = row.get("location")
        if location is None:
            continue
        totals[int(location)] = totals.get(int(location), 0.0) + float(row.get("amount", 0) or 0)
    ranked = sorted(totals.items(), key=lambda item: item[1], reverse=True)[:limit]
    return [{"region": region, "amount": amount} for region, amount in ranked]


def _top_event_regions(rows: list[dict], event_type: str, limit: int) -> list[dict]:
    totals: dict[int, float] = {}
    for row in rows:
        if row.get("type") != event_type or row.get("location") is None:
            continue
        location = int(row["location"])
        totals[location] = totals.get(location, 0.0) + float(row.get("amount", 0) or 0)
    ranked = sorted(totals.items(), key=lambda item: item[1], reverse=True)[:limit]
    return [{"region": region, "amount": amount} for region, amount in ranked]


def _ranked_region_lines(rows: list[dict]) -> list[str]:
    return [f"- {idx}위: {row['region']}번 구역 / 누적 {row['amount']:.0f}" for idx, row in enumerate(rows, 1)]


def _recommendations_md(bundle: dict) -> str:
    d = bundle["diagnosis"]
    return "\n".join(
        [
            "# Strategy Recommendations",
            "",
            f"1. {d.get('recommended_action')}",
            "2. `analysis_report.md`의 골드, 병력, 건물 차트를 기준으로 분기점 전 10턴의 명령을 검토하세요.",
            "3. `buildup_timeline.csv`에서 승리 진영의 초반 행동과 패배 진영의 대응 누락을 비교하세요.",
        ]
    )

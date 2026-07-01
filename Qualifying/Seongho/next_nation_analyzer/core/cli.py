from __future__ import annotations

import argparse
import json
from pathlib import Path

from .buildup import build_buildup_timeline
from .diagnosis import diagnose
from .gold_tracker import estimate_gold
from .ingest import read_input
from .metrics import army_rows, building_rows, compute_metrics, damage_rows, unit_path_rows
from .parser import parse_log
from .report import write_outputs
from .state_replay import replay_state
from .validate import validate_game
from .visualize import generate_charts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="next_nation_analyzer")
    sub = parser.add_subparsers(dest="command", required=True)
    analyze = sub.add_parser("analyze", help="Analyze one NEXT NATION .txt/.log file")
    analyze.add_argument("log_file")
    analyze.add_argument("--out")
    analyze.add_argument("--strict", action="store_true")
    analyze.add_argument("--force", action="store_true")
    analyze.add_argument("--format", choices=["all", "json", "csv", "md"], default="all")
    analyze.add_argument("--rules-version")
    analyze.add_argument("--main-factor-threshold", type=float, default=0.5)

    chart = sub.add_parser("chart", help="Regenerate charts from game_analysis.json")
    chart.add_argument("game_analysis_json")
    chart.add_argument("--out")

    args = parser.parse_args(argv)
    if args.command == "analyze":
        return analyze_one(args)
    if args.command == "chart":
        return chart_only(args)
    return 2


def analyze_one(args) -> int:
    source, text, manifest = read_input(args.log_file)
    if source.suffix.lower() not in {".txt", ".log"}:
        print("UNSUPPORTED_FORMAT: input must be .txt or .log")
        return 2
    out_dir = Path(args.out) if args.out else source.parent / "out"
    cache_file = out_dir / "raw_data" / "input_manifest.json"
    if not cache_file.exists():
        cache_file = out_dir / "input_manifest.json"
    if cache_file.exists() and not args.force:
        try:
            cached = json.loads(cache_file.read_text(encoding="utf-8"))
            if cached.get("source_hash") == manifest["source_hash"]:
                print(f"Cache hit: {out_dir}")
                return 0
        except Exception:
            pass

    game = parse_log(text, str(source), manifest["source_hash"])
    validation = validate_game(game)
    if args.strict and validation["status"] != "VALID":
        print(f"Strict validation failed: {validation['status']}")
        return 1

    replay = replay_state(game)
    gold_rows, gold_details = estimate_gold(game, replay)
    metrics = compute_metrics(game, replay, gold_rows)
    army = army_rows(replay)
    buildings = building_rows(replay)
    damage = damage_rows(replay)
    unit_paths = unit_path_rows(replay)
    diagnosis = diagnose(game, replay, metrics, gold_rows)
    buildup = build_buildup_timeline(game, replay, diagnosis)
    chart_data = {
        "map": game.map.to_dict(),
        "events": metrics["events_rows"],
        "gold_rows": gold_rows,
        "army_rows": army,
        "building_rows": buildings,
        "damage_rows": damage,
        "unit_path_rows": unit_paths,
        "combat_zones": replay.get("combat_zones", []),
        "diagnosis": diagnosis,
        "buildup": buildup,
    }
    game_analysis = {
        **game.to_dict(),
        "validation": validation,
        "metrics": {
            "command_counts": metrics["command_counts"],
            "event_counts": metrics["event_counts"],
            "first_turns": metrics["first_turns"],
        },
        "diagnosis": diagnosis,
    }
    bundle = {
        "manifest": manifest,
        "game": game,
        "game_analysis": game_analysis,
        "validation": validation,
        "replay": replay,
        "metrics": metrics,
        "gold_rows": gold_rows,
        "gold_details": gold_details,
        "army_rows": army,
        "building_rows": buildings,
        "damage_rows": damage,
        "unit_path_rows": unit_paths,
        "diagnosis": diagnosis,
        "buildup": buildup,
        "chart_data": chart_data,
    }
    write_outputs(out_dir, bundle)
    made = generate_charts(out_dir, chart_data)
    status_file = out_dir / "chart_generation_status.json"
    if status_file.exists():
        raw_dir = out_dir / "raw_data"
        raw_dir.mkdir(exist_ok=True)
        (raw_dir / "chart_generation_status.json").write_text(status_file.read_text(encoding="utf-8"), encoding="utf-8")
        status_file.unlink()
    print(f"Analyzed {source.name}: {game.validation_status}")
    print(f"Output: {out_dir}")
    print(f"Charts: {len(made)}")
    return 0


def chart_only(args) -> int:
    analysis_path = Path(args.game_analysis_json)
    out_dir = Path(args.out) if args.out else analysis_path.parent
    chart_data_path = analysis_path.parent / "chart_data.json"
    if not chart_data_path.exists():
        chart_data_path = analysis_path.parent / "raw_data" / "chart_data.json"
    if analysis_path.parent.name == "raw_data":
        out_dir = analysis_path.parent.parent
    if not chart_data_path.exists():
        print(f"Missing chart_data.json next to {analysis_path} or in raw_data/")
        return 1
    chart_data = json.loads(chart_data_path.read_text(encoding="utf-8"))
    made = generate_charts(out_dir, chart_data)
    status_file = out_dir / "chart_generation_status.json"
    if status_file.exists():
        raw_dir = out_dir / "raw_data"
        raw_dir.mkdir(exist_ok=True)
        (raw_dir / "chart_generation_status.json").write_text(status_file.read_text(encoding="utf-8"), encoding="utf-8")
        status_file.unlink()
    print(f"Generated {len(made)} charts in {out_dir / 'charts'}")
    return 0

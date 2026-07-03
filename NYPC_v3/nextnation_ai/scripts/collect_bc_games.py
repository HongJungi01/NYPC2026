#!/usr/bin/env python3
"""Collect teacher games for behavior-cloning pretraining (BC_SPEC.md 1).

Plays the teacher bot (default manual_model/hjk-07022029f.cpp) against a
diverse opponent set — itself, every other compiled manual bot, lth-v4-4,
random, and an always-empty idle bot — and records the map plus both sides'
as-simulated commands per day, so the exact states can be replayed later with
the judge-exact Simulator. Every game is replay-verified before it is written.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from nextnation_ai.agents.cpp_manual_agent import ManualCppAgent
from nextnation_ai.agents.manual_agent import ManualPythonAgent
from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.simulator import Simulator
from nextnation_ai.game.state import GameState, Side, UnitId, create_initial_state

ROOT = Path(__file__).resolve().parents[2]

# Full-strength timeouts for data collection; training-time opponents keep the
# contest-like 0.5 s default in ManualCppAgent.
HANDSHAKE_TIMEOUT = 10.0
COMMAND_TIMEOUT = 10.0


class IdleAgent:
    """Always-empty opponent: the turtle the trained policy must punish."""

    def act(self, state: GameState, player: Side) -> CommandAction:
        return CommandAction()


def action_to_json(action: CommandAction) -> list:
    return [
        action.train_n,
        list(action.upgrades),
        [[str(m.unit_id), m.target] for m in action.moves],
    ]


def action_from_json(data: list) -> CommandAction:
    train_n, upgrades, moves = data
    return CommandAction(
        int(train_n),
        [int(r) for r in upgrades],
        [MoveCommand(UnitId.parse(uid), int(target)) for uid, target in moves],
    )


def state_signature(state: GameState) -> tuple:
    return (
        state.day,
        state.done,
        None if state.winner is None else state.winner.value,
        state.player(Side.LEFT).gold,
        state.player(Side.RIGHT).gold,
        tuple(sorted((str(u.id), u.pos, u.hp, u.target) for u in state.units.values())),
        tuple(sorted((r, b.owner.value, b.kind.value, b.level, b.hp) for r, b in state.buildings.items())),
    )


def replay_game(game_map: GameMap, actions: list[list[list]]) -> GameState:
    sim = Simulator()
    state = create_initial_state(game_map)
    for left_json, right_json in actions:
        result = sim.step(state, action_from_json(left_json), action_from_json(right_json))
        state = result.state
        if state.done:
            break
    return state


@dataclass(frozen=True)
class GameJob:
    map_rows: tuple[str, ...]
    teacher_path: str
    opponent_kind: str  # "mirror" | "cpp" | "python" | "random" | "idle"
    opponent_path: str | None
    opponent_name: str
    teacher_side: int  # Side value; ignored for mirror (teacher plays both)
    seed: int


def _make_opponent(job: GameJob):
    if job.opponent_kind == "mirror":
        return ManualCppAgent(job.teacher_path, handshake_timeout=HANDSHAKE_TIMEOUT, command_timeout=COMMAND_TIMEOUT)
    if job.opponent_kind == "cpp":
        return ManualCppAgent(job.opponent_path, handshake_timeout=HANDSHAKE_TIMEOUT, command_timeout=COMMAND_TIMEOUT)
    if job.opponent_kind == "python":
        return ManualPythonAgent(Path(job.opponent_path))
    if job.opponent_kind == "random":
        return RandomLegalAgent(seed=job.seed, upgrade_probability=0.2, move_probability=0.25)
    if job.opponent_kind == "idle":
        return IdleAgent()
    raise ValueError(job.opponent_kind)


def run_game(job: GameJob) -> dict:
    game_map = GameMap.from_lines(list(job.map_rows))
    teacher_side = Side(job.teacher_side)
    teacher = ManualCppAgent(job.teacher_path, handshake_timeout=HANDSHAKE_TIMEOUT, command_timeout=COMMAND_TIMEOUT)
    opponent = _make_opponent(job)
    teacher_sides = [Side.LEFT, Side.RIGHT] if job.opponent_kind == "mirror" else [teacher_side]
    agents = {teacher_sides[0]: teacher}
    for side in (Side.LEFT, Side.RIGHT):
        if side not in agents:
            agents[side] = opponent

    interactive = [side for side, agent in agents.items() if isinstance(agent, ManualCppAgent)]
    try:
        for side in interactive:
            agents[side].start_episode(game_map, side)
        sim = Simulator()
        state = create_initial_state(game_map)
        actions: list[list[list]] = []
        while not state.done:
            day_actions = {}
            for side in (Side.LEFT, Side.RIGHT):
                try:
                    day_actions[side] = agents[side].act(state, side)
                except Exception:
                    day_actions[side] = CommandAction()
            result = sim.step(state, day_actions[Side.LEFT], day_actions[Side.RIGHT])
            if result.invalid_sides:
                raise RuntimeError(f"invalid action from {result.invalid_sides} on day {state.day + 1}")
            state = result.state
            actions.append([action_to_json(day_actions[Side.LEFT]), action_to_json(day_actions[Side.RIGHT])])
            for side in interactive:
                agents[side].observe(state, side, result.events)
    finally:
        for side in interactive:
            try:
                agents[side].finish()
            except Exception:
                pass

    if state_signature(replay_game(game_map, actions)) != state_signature(state):
        raise RuntimeError("replay signature mismatch — game not deterministic")

    winner = "DRAW" if state.winner is None else state.winner.word
    return {
        "map_rows": list(job.map_rows),
        "teacher_sides": [s.value for s in teacher_sides],
        "opponent": job.opponent_name,
        "seed": job.seed,
        "days": state.day,
        "winner": winner,
        "teacher_won": None if job.opponent_kind == "mirror" else (state.winner is teacher_side),
        "reason": state.terminal_reason,
        "actions": actions,
    }


def compile_cpp(src: Path, build_dir: Path) -> Path:
    build_dir.mkdir(parents=True, exist_ok=True)
    out = build_dir / src.stem
    proc = subprocess.run(
        ["g++", "-O2", "-std=c++17", str(src), "-o", str(out)],
        cwd=ROOT, text=True, capture_output=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"compile failed for {src.name}:\n{proc.stderr[:2000]}")
    return out


def generate_map_rows(count: int, seed: int, np_: int, kp: int) -> list[list[str]]:
    import importlib.util

    spec = importlib.util.spec_from_file_location("nextnation_testing_tool", ROOT / "nation-providing" / "testing-tool.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return [module.generate_map(module.XoShiro256(seed + i), NP=np_, KP=kp) for i in range(count)]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--teacher", type=Path, default=ROOT / "manual_model" / "hjk-07022029f.cpp")
    parser.add_argument("--out", type=Path, default=ROOT / "runs" / "bc_data" / "games.jsonl")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "runs" / "bc_data" / "bins")
    parser.add_argument("--maps", type=int, default=25)
    parser.add_argument("--np", type=int, default=25, dest="np_")
    parser.add_argument("--kp", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20260702)
    parser.add_argument("--games-per-opponent", type=int, default=50,
                        help="Games per opponent kind (split half/half across teacher sides).")
    parser.add_argument("--workers", type=int, default=8)
    args = parser.parse_args()

    teacher_bin = compile_cpp(args.teacher, args.build_dir)
    opponents: list[tuple[str, str, str | None]] = [("mirror", "mirror", None)]
    for src in sorted((ROOT / "manual_model").glob("*.cpp")):
        if src.resolve() == args.teacher.resolve():
            continue
        opponents.append(("cpp", src.stem, str(compile_cpp(src, args.build_dir))))
    opponents.append(("python", "lth-v4-4", str(ROOT / "manual_model" / "lth-v4-4.py")))
    opponents.append(("random", "random", None))
    opponents.append(("idle", "idle", None))

    map_rows = generate_map_rows(args.maps, args.seed, args.np_, args.kp)
    jobs: list[GameJob] = []
    for opp_index, (kind, name, path) in enumerate(opponents):
        # Every bot in manual_model/ is deterministic (no RNG, no clock), so
        # unique games are bounded by the map pool: maps x 2 sides per
        # opponent, and just maps for the mirror (side is irrelevant there).
        # Only the seeded random opponent scales beyond that.
        n_games = args.games_per_opponent
        if kind == "mirror":
            n_games = min(n_games, len(map_rows))
        elif kind != "random":
            n_games = min(n_games, 2 * len(map_rows))
        for g in range(n_games):
            if kind == "mirror":
                map_index = g % len(map_rows)
            elif kind == "random":
                map_index = (g // 2) % len(map_rows)
            else:
                map_index = g // 2  # (map, side) pairs enumerated exactly once
            jobs.append(
                GameJob(
                    map_rows=tuple(map_rows[map_index]),
                    teacher_path=str(teacher_bin),
                    opponent_kind=kind,
                    opponent_path=path,
                    opponent_name=name,
                    teacher_side=(Side.LEFT if g % 2 == 0 else Side.RIGHT).value,
                    seed=args.seed + opp_index * 100000 + g,
                )
            )

    args.out.parent.mkdir(parents=True, exist_ok=True)
    started = time.time()
    ok = failed = 0
    outcomes: dict[str, list[bool | None]] = {}
    with args.out.open("w", encoding="utf-8") as out:
        with ProcessPoolExecutor(max_workers=args.workers) as pool:
            for job, result in zip(jobs, pool.map(_run_game_safe, jobs)):
                if isinstance(result, str):
                    failed += 1
                    print(f"FAILED {job.opponent_name} seed={job.seed}: {result}", flush=True)
                    continue
                ok += 1
                outcomes.setdefault(job.opponent_name, []).append(result["teacher_won"])
                out.write(json.dumps(result, ensure_ascii=False) + "\n")
                if ok % 25 == 0:
                    print(f"{ok}/{len(jobs)} games ({time.time() - started:.0f}s)", flush=True)

    print(f"done: {ok} games written, {failed} failed, {time.time() - started:.0f}s -> {args.out}")
    for name, results in sorted(outcomes.items()):
        wins = sum(1 for r in results if r)
        known = [r for r in results if r is not None]
        rate = f"{wins}/{len(known)}" if known else "mirror"
        print(f"  teacher vs {name:<14} games={len(results)} wins={rate}")


def _run_game_safe(job: GameJob) -> dict | str:
    try:
        return run_game(job)
    except Exception as exc:  # noqa: BLE001 — collection must survive bot crashes
        return f"{type(exc).__name__}: {exc}"


if __name__ == "__main__":
    main()

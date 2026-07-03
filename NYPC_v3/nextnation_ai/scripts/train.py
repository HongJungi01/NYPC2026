#!/usr/bin/env python3
"""Train the dependency-free neural macro policy with league self-play."""

from __future__ import annotations

import argparse
import importlib.util
import json
import random
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from nextnation_ai.agents.manual_agent import ManualPythonAgent
from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game.env import NextNationEnv
from nextnation_ai.game.map import GameMap, make_line_map
from nextnation_ai.game.state import Side
from nextnation_ai.nn.simple_policy import ACTION_NAMES, SimpleNeuralPolicy


ROOT = Path(__file__).resolve().parents[2]
TESTING_TOOL = ROOT / "nation-providing" / "testing-tool.py"
RUN_DIR = ROOT / "runs" / "simple_policy"


@dataclass
class EpisodeStats:
    opponent: str
    current_side: str
    winner: str
    reason: str | None
    days: int
    reward: float
    action_counts: dict[str, int]


def _load_testing_tool_module():
    spec = importlib.util.spec_from_file_location("nextnation_testing_tool", TESTING_TOOL)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot import {TESTING_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def generate_maps(count: int, seed: int, *, np_: int = 25, kp: int = 4) -> list[GameMap]:
    module = _load_testing_tool_module()
    maps: list[GameMap] = []
    for i in range(count):
        rows = module.generate_map(module.XoShiro256(seed + i), NP=np_, KP=kp)
        maps.append(GameMap.from_lines(rows))
    return maps


def opponent_action(opponent: Any, state, player: Side):
    if isinstance(opponent, SimpleNeuralPolicy):
        return opponent.select_action(state, player, deterministic=True).action
    return opponent.act(state, player)


def play_episode(
    policy: SimpleNeuralPolicy,
    opponent: Any,
    opponent_name: str,
    game_map: GameMap,
    *,
    current_side: Side | None,
    max_days: int,
    rng: random.Random,
    temperature: float,
    collect: bool,
) -> tuple[list[list[dict]], EpisodeStats]:
    env = NextNationEnv(game_map)
    env.reset(game_map)
    trajectories: dict[Side, list[dict]] = {Side.LEFT: [], Side.RIGHT: []}
    controlled = {Side.LEFT, Side.RIGHT} if current_side is None else {current_side}
    action_counts = {name: 0 for name in ACTION_NAMES}
    total_reward = 0.0

    while not env.state.done and env.state.day < max_days:
        decisions = {}
        if Side.LEFT in controlled:
            decisions[Side.LEFT] = policy.select_action(env.state, Side.LEFT, temperature=temperature, rng=rng)
            left_action = decisions[Side.LEFT].action
        else:
            left_action = opponent_action(opponent, env.state, Side.LEFT)

        if Side.RIGHT in controlled:
            decisions[Side.RIGHT] = policy.select_action(env.state, Side.RIGHT, temperature=temperature, rng=rng)
            right_action = decisions[Side.RIGHT].action
        else:
            right_action = opponent_action(opponent, env.state, Side.RIGHT)

        step = env.step(left_action, right_action)
        rewards = {Side.LEFT: step.reward_left, Side.RIGHT: step.reward_right}
        for side, decision in decisions.items():
            action_counts[ACTION_NAMES[decision.action_index]] += 1
            total_reward += rewards[side]
            if collect:
                trajectories[side].append({
                    "features": decision.features,
                    "legal_mask": decision.legal_mask,
                    "action_index": decision.action_index,
                    "value": decision.value,
                    "logp": decision.logp,
                    "entropy": decision.entropy,
                    "reward": rewards[side],
                })

    if env.state.winner is Side.LEFT:
        winner = "LEFT"
    elif env.state.winner is Side.RIGHT:
        winner = "RIGHT"
    else:
        winner = "DRAW"
    if current_side is None:
        side_label = "both"
    else:
        side_label = current_side.word
    kept = [traj for side, traj in trajectories.items() if side in controlled and traj]
    return kept, EpisodeStats(
        opponent=opponent_name,
        current_side=side_label,
        winner=winner,
        reason=env.state.terminal_reason,
        days=env.state.day,
        reward=total_reward,
        action_counts=action_counts,
    )


def choose_opponent(
    rng: random.Random,
    manual_agent: ManualPythonAgent,
    random_agent: RandomLegalAgent,
    snapshots: list[SimpleNeuralPolicy],
) -> tuple[str, Any, Side | None]:
    choices: list[tuple[float, str, Any, Side | None]] = [
        (0.35, "manual:lth-v4-4", manual_agent, rng.choice([Side.LEFT, Side.RIGHT])),
        (0.15, "random", random_agent, rng.choice([Side.LEFT, Side.RIGHT])),
        (0.20, "self:current", None, None),
    ]
    if snapshots:
        snap = rng.choice(snapshots)
        choices.append((0.30, "snapshot", snap, rng.choice([Side.LEFT, Side.RIGHT])))
    total = sum(w for w, *_ in choices)
    r = rng.random() * total
    acc = 0.0
    for weight, name, opponent, side in choices:
        acc += weight
        if r <= acc:
            if name == "self:current":
                return name, None, None
            current_side = side
            assert current_side is not None
            # current_side means the learning policy controls that side; opponent controls the other.
            return name, opponent, current_side
    return "manual:lth-v4-4", manual_agent, Side.LEFT


def summarize_eval(stats: list[EpisodeStats]) -> dict[str, Any]:
    by_opp: dict[str, dict[str, Any]] = {}
    for st in stats:
        cur = by_opp.setdefault(st.opponent, {"games": 0, "wins": 0, "losses": 0, "draws": 0, "avg_days": 0.0})
        cur["games"] += 1
        cur["avg_days"] += st.days
        if st.winner == "DRAW":
            cur["draws"] += 1
        elif st.current_side == "both":
            cur["draws"] += 1
        elif st.winner == st.current_side:
            cur["wins"] += 1
        else:
            cur["losses"] += 1
    for cur in by_opp.values():
        cur["avg_days"] /= max(1, cur["games"])
        cur["win_rate"] = cur["wins"] / max(1, cur["games"])
    return by_opp


def evaluate_policy(
    policy: SimpleNeuralPolicy,
    eval_maps: list[GameMap],
    manual_agent: ManualPythonAgent,
    random_agent: RandomLegalAgent,
    snapshots: list[SimpleNeuralPolicy],
    *,
    max_days: int,
    rng: random.Random,
) -> dict[str, Any]:
    opponents: list[tuple[str, Any]] = [("manual:lth-v4-4", manual_agent), ("random", random_agent)]
    if snapshots:
        opponents.append(("latest_snapshot", snapshots[-1]))
    stats: list[EpisodeStats] = []
    for name, opponent in opponents:
        for game_map in eval_maps[:2]:
            for side in (Side.LEFT, Side.RIGHT):
                _traj, st = play_episode(
                    policy,
                    opponent,
                    name,
                    game_map,
                    current_side=side,
                    max_days=max_days,
                    rng=rng,
                    temperature=0.5,
                    collect=False,
                )
                stats.append(st)
    return summarize_eval(stats)


def save_checkpoint(policy: SimpleNeuralPolicy, ckpt_dir: Path, update: int, metrics: dict[str, Any]) -> Path:
    ckpt_dir.mkdir(parents=True, exist_ok=True)
    path = ckpt_dir / f"policy_update_{update:05d}.json"
    policy.save(path, {"update": update, "metrics": metrics})
    policy.save(ckpt_dir / "latest.json", {"update": update, "metrics": metrics})
    return path


def compile_manual_cpp(build_dir: Path) -> dict[str, str]:
    build_dir.mkdir(parents=True, exist_ok=True)
    compiled: dict[str, str] = {}
    for src in sorted((ROOT / "manual_model").glob("*.cpp")):
        out = build_dir / src.stem
        cmd = ["g++", "-O2", "-std=c++17", str(src), "-o", str(out)]
        result = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True)
        compiled[src.name] = "ok" if result.returncode == 0 else (result.stderr.strip() or "compile failed")
    return compiled


def external_evaluate_checkpoint(
    checkpoint: Path,
    manual_bin_dir: Path,
    *,
    games: int,
    seed: int,
) -> dict[str, Any]:
    bins = sorted(p for p in manual_bin_dir.glob("hjk-*") if p.is_file())
    if not bins or games <= 0:
        return {}
    result: dict[str, Any] = {}
    for bin_path in bins:
        rec = {"games": 0, "wins": 0, "losses": 0, "draws": 0, "results": []}
        for i in range(games):
            current_side = Side.LEFT if i % 2 == 0 else Side.RIGHT
            policy_cmd = f"python3 -B -m nextnation_ai.submit.policy_bot --checkpoint {checkpoint}"
            manual_cmd = str(bin_path)
            if current_side is Side.LEFT:
                exec_a, exec_b = policy_cmd, manual_cmd
            else:
                exec_a, exec_b = manual_cmd, policy_cmd
            log_file = ROOT / "runs" / "external_eval_tmp.log"
            cmd = [
                "python3", "-B", str(TESTING_TOOL),
                "--seed", str(seed + i), "--NP", "25", "--KP", "4",
                "-l", str(log_file),
                "-a", exec_a,
                "-b", exec_b,
            ]
            try:
                proc = subprocess.run(cmd, cwd=ROOT, text=True, capture_output=True, timeout=60)
                line = (proc.stdout.strip().splitlines() or [""])[-1]
            except subprocess.TimeoutExpired:
                line = "RESULT TIMEOUT"
            rec["games"] += 1
            rec["results"].append(line)
            if "DRAW" in line:
                rec["draws"] += 1
            elif (current_side is Side.LEFT and "LEFT_WIN" in line) or (current_side is Side.RIGHT and "RIGHT_WIN" in line):
                rec["wins"] += 1
            else:
                rec["losses"] += 1
        rec["win_rate"] = rec["wins"] / max(1, rec["games"])
        result[bin_path.name] = rec
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--updates", type=int, default=20)
    parser.add_argument("--episodes-per-update", type=int, default=6)
    parser.add_argument("--max-days", type=int, default=120)
    parser.add_argument("--eval-days", type=int, default=200)
    parser.add_argument("--map-count", type=int, default=6)
    parser.add_argument("--seed", type=int, default=20260701)
    parser.add_argument("--lr", type=float, default=0.015)
    parser.add_argument("--gamma", type=float, default=0.992)
    parser.add_argument("--temperature", type=float, default=1.0)
    parser.add_argument("--checkpoint-interval", type=int, default=5)
    parser.add_argument("--snapshot-interval", type=int, default=4)
    parser.add_argument("--eval-interval", type=int, default=5)
    parser.add_argument("--run-dir", type=Path, default=RUN_DIR)
    parser.add_argument("--compile-manual-cpp", action="store_true")
    parser.add_argument("--external-eval-interval", type=int, default=0)
    parser.add_argument("--external-eval-games", type=int, default=1)
    args = parser.parse_args()

    rng = random.Random(args.seed)
    args.run_dir.mkdir(parents=True, exist_ok=True)
    ckpt_dir = args.run_dir / "checkpoints"
    log_path = args.run_dir / "train_log.jsonl"
    train_maps = generate_maps(args.map_count, args.seed, np_=25, kp=4)
    eval_maps = generate_maps(max(2, min(4, args.map_count)), args.seed + 10_000, np_=25, kp=4)

    feature_dim = 26
    policy = SimpleNeuralPolicy(input_dim=feature_dim, hidden_dim=32, seed=args.seed)
    manual_agent = ManualPythonAgent()
    random_agent = RandomLegalAgent(seed=args.seed + 1, upgrade_probability=0.2, move_probability=0.25)
    snapshots: list[SimpleNeuralPolicy] = []

    metadata: dict[str, Any] = {
        "seed": args.seed,
        "updates": args.updates,
        "episodes_per_update": args.episodes_per_update,
        "max_days": args.max_days,
        "action_names": ACTION_NAMES,
        "manual_python": str(manual_agent.path),
        "torch_available": False,
    }
    manual_bin_dir = args.run_dir / "manual_bins"
    if args.compile_manual_cpp:
        metadata["manual_cpp_compile"] = compile_manual_cpp(manual_bin_dir)

    with log_path.open("a", encoding="utf-8") as log:
        log.write(json.dumps({"type": "metadata", **metadata}, ensure_ascii=False) + "\n")
        last_eval: dict[str, Any] = {}
        for update in range(1, args.updates + 1):
            started = time.time()
            trajectories: list[list[dict]] = []
            ep_stats: list[EpisodeStats] = []
            for ep in range(args.episodes_per_update):
                name, opponent, current_side = choose_opponent(rng, manual_agent, random_agent, snapshots)
                if opponent is None:
                    opponent = policy
                game_map = rng.choice(train_maps)
                trajs, stats = play_episode(
                    policy,
                    opponent,
                    name,
                    game_map,
                    current_side=current_side,
                    max_days=args.max_days,
                    rng=rng,
                    temperature=args.temperature,
                    collect=True,
                )
                trajectories.extend(trajs)
                ep_stats.append(stats)

            train_stats = policy.update_trajectories(
                trajectories,
                lr=args.lr,
                gamma=args.gamma,
                value_coef=0.5,
            )
            action_counts = {name: 0 for name in ACTION_NAMES}
            for st in ep_stats:
                for name, count in st.action_counts.items():
                    action_counts[name] += count
            opponents = summarize_eval(ep_stats)

            if update % args.snapshot_interval == 0:
                snapshots.append(policy.copy())
                snapshots[:] = snapshots[-8:]

            if update % args.eval_interval == 0 or update == 1:
                last_eval = evaluate_policy(
                    policy,
                    eval_maps,
                    manual_agent,
                    random_agent,
                    snapshots,
                    max_days=args.eval_days,
                    rng=rng,
                )

            metrics: dict[str, Any] = {
                "type": "update",
                "update": update,
                "elapsed_sec": round(time.time() - started, 3),
                "train": train_stats,
                "train_opponents": opponents,
                "action_counts": action_counts,
                "snapshots": len(snapshots),
                "eval": last_eval,
            }
            checkpoint_path = None
            if update % args.checkpoint_interval == 0 or update == 1 or update == args.updates:
                checkpoint_path = save_checkpoint(policy, ckpt_dir, update, metrics)
                metrics["checkpoint"] = str(checkpoint_path)
            if (checkpoint_path is not None and args.external_eval_interval > 0
                    and update % args.external_eval_interval == 0 and manual_bin_dir.exists()):
                metrics["external_eval"] = external_evaluate_checkpoint(
                    checkpoint_path,
                    manual_bin_dir,
                    games=args.external_eval_games,
                    seed=args.seed + update * 1000,
                )
                policy.save(checkpoint_path, {"update": update, "metrics": metrics})
                policy.save(ckpt_dir / "latest.json", {"update": update, "metrics": metrics})
            log.write(json.dumps(metrics, ensure_ascii=False) + "\n")
            log.flush()
            print(json.dumps(metrics, ensure_ascii=False))


if __name__ == "__main__":
    main()

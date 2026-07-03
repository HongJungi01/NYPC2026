#!/usr/bin/env python3
"""Behavior-cloning pretraining from collected teacher games (BC_SPEC.md 2).

Replays games recorded by collect_bc_games.py through the judge-exact
Simulator, emits one item per teacher-side day — including days with empty
actions, which are real "save gold / nothing to do" targets — and trains the
policy heads with the same imitation cross-entropy used by PPO, plus a value
regression to the discounted shaped return, plus a small entropy bonus so the
heads stay soft for the PPO handoff.

The saved checkpoints are train_torch-compatible (--init-checkpoint).
"""

from __future__ import annotations

import argparse
import json
import random
import time
from pathlib import Path

import torch

from nextnation_ai.game import observation as observation_features
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.reward import shaped_reward
from nextnation_ai.game.simulator import Simulator
from nextnation_ai.game.state import GameState, Side, create_initial_state
from nextnation_ai.nn.torch_policy import (
    NextNationGNNPolicy,
    PreparedBatch,
    action_components_from_action,
    evaluate_prepared_batch,
    prepare_evaluation_batch,
    select_action,
)
from nextnation_ai.scripts.collect_bc_games import IdleAgent, action_from_json
from nextnation_ai.agents.random_agent import RandomLegalAgent

ROOT = Path(__file__).resolve().parents[2]


def replay_game_items(record: dict, beta: float, gamma: float):
    """(state, side, teacher components, discounted shaped return) per teacher day."""
    game_map = GameMap.from_lines(record["map_rows"])
    teacher_sides = [Side(v) for v in record["teacher_sides"]]
    sim = Simulator()
    state = create_initial_state(game_map)
    items: dict[Side, list] = {side: [] for side in teacher_sides}
    rewards: dict[Side, list[float]] = {side: [] for side in teacher_sides}
    for left_json, right_json in record["actions"]:
        before = state.clone()
        actions = {Side.LEFT: action_from_json(left_json), Side.RIGHT: action_from_json(right_json)}
        result = sim.step(state, actions[Side.LEFT], actions[Side.RIGHT])
        state = result.state
        for side in teacher_sides:
            comp = action_components_from_action(before, side, actions[side])
            items[side].append((before, side, comp, comp))
            rewards[side].append(shaped_reward(before, state, side, beta=beta))
        if state.done:
            break
    out = []
    for side in teacher_sides:
        g = 0.0
        returns: list[float] = []
        for r in reversed(rewards[side]):
            g = r + gamma * g
            returns.append(g)
        returns.reverse()
        out.extend(zip(items[side], returns))
    return out


def batch_to_device(batch: PreparedBatch, device: torch.device) -> PreparedBatch:
    return PreparedBatch(
        {k: v.to(device, non_blocking=True) for k, v in batch.features.items()},
        batch.n_max,
        {k: tuple(t.to(device, non_blocking=True) for t in g) for k, g in batch.grids.items()},
        {k: tuple(t.to(device, non_blocking=True) for t in g) for k, g in batch.im_grids.items()},
        batch.im_present.to(device, non_blocking=True),
    )


def prepare_chunks(records: list[dict], *, beta: float, gamma: float, batch_size: int, seed: int, label: str):
    started = time.time()
    all_items: list = []
    all_returns: list[float] = []
    for record in records:
        for item, ret in replay_game_items(record, beta, gamma):
            all_items.append(item)
            all_returns.append(ret)
    order = list(range(len(all_items)))
    random.Random(seed).shuffle(order)
    chunks = []
    cpu = torch.device("cpu")
    for lo in range(0, len(order), batch_size):
        idx = order[lo : lo + batch_size]
        batch = prepare_evaluation_batch([all_items[i] for i in idx], cpu)
        returns = torch.tensor([all_returns[i] for i in idx], dtype=torch.float32)
        chunks.append((batch, returns))
    print(f"{label}: {len(all_items)} items -> {len(chunks)} chunks ({time.time() - started:.0f}s)", flush=True)
    return chunks


def run_epoch(model, chunks, device, *, optimizer=None, value_coef: float, entropy_coef: float, max_grad_norm: float = 0.5, rng=None):
    training = optimizer is not None
    model.train(training)
    ce_sum = mse_sum = ent_sum = n = 0.0
    order = list(range(len(chunks)))
    if training and rng is not None:
        rng.shuffle(order)
    for i in order:
        batch, returns = chunks[i]
        batch_dev = batch_to_device(batch, device)
        returns_dev = returns.to(device)
        with torch.set_grad_enabled(training):
            ev = evaluate_prepared_batch(model, batch_dev)
            ce = ev.imitation_per_item.sum() / ev.imitation_present.sum().clamp_min(1.0)
            mse = ((ev.value - returns_dev) ** 2).mean()
            entropy = ev.entropy.mean()
            loss = ce + value_coef * mse - entropy_coef * entropy
        if training:
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), max_grad_norm)
            optimizer.step()
        b = batch.size
        ce_sum += float(ce.detach()) * b
        mse_sum += float(mse.detach()) * b
        ent_sum += float(entropy.detach()) * b
        n += b
    return {"ce": ce_sum / n, "value_mse": mse_sum / n, "entropy": ent_sum / n, "items": int(n)}


def probe_rollouts(model, device, maps, opponent_name: str, seed: int, max_days: int = 200):
    """Greedy games vs a scripted opponent; behavior stats for acceptance."""
    stats = {"games": 0, "wins": 0, "upgrade_days": 0, "nonempty_days": 0, "days": 0}
    for map_index, game_map in enumerate(maps):
        for side in (Side.LEFT, Side.RIGHT):
            opponent = (
                IdleAgent()
                if opponent_name == "idle"
                else RandomLegalAgent(seed=seed + map_index * 2 + int(side), upgrade_probability=0.2, move_probability=0.25)
            )
            sim = Simulator()
            state = create_initial_state(game_map)
            while not state.done and state.day < max_days:
                with torch.no_grad():
                    my_action = select_action(model, state, side, device, deterministic=True).action
                opp_action = opponent.act(state, side.opponent)
                if my_action.train_n or my_action.upgrades or my_action.moves:
                    stats["nonempty_days"] += 1
                if my_action.upgrades:
                    stats["upgrade_days"] += 1
                ordered = (my_action, opp_action) if side is Side.LEFT else (opp_action, my_action)
                state = sim.step(state, *ordered).state
            stats["games"] += 1
            stats["days"] += state.day
            stats["wins"] += 1 if state.winner is side else 0
    games = stats["games"]
    return {
        "opponent": opponent_name,
        "games": games,
        "wins": stats["wins"],
        "avg_days": stats["days"] / games,
        "upgrade_days_per_game": stats["upgrade_days"] / games,
        "nonempty_days_per_game": stats["nonempty_days"] / games,
        "nonempty_ratio": stats["nonempty_days"] / max(1, stats["days"]),
    }


def save_checkpoint(path: Path, model, optimizer, metrics: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {"update": 0, "model_state": model.state_dict(), "optimizer_state": optimizer.state_dict(), "metrics": metrics},
        path,
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, default=ROOT / "runs" / "bc_data" / "games.jsonl")
    parser.add_argument("--run-dir", type=Path, default=ROOT / "runs" / "bc_pretrain")
    parser.add_argument("--epochs", type=int, default=20)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--value-coef", type=float, default=0.5)
    parser.add_argument("--entropy-coef", type=float, default=0.005)
    parser.add_argument("--shaping-beta", type=float, default=0.18)
    parser.add_argument("--gamma", type=float, default=0.997)
    parser.add_argument("--val-frac", type=float, default=0.1)
    parser.add_argument("--seed", type=int, default=20260702)
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--probe-interval", type=int, default=2)
    parser.add_argument("--probe-maps", type=int, default=2)
    parser.add_argument("--enemy-gold-noise", action=argparse.BooleanOptionalAction, default=True)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    if args.enemy_gold_noise:
        observation_features.ENEMY_GOLD_NOISE = (0.8, 1.3, 100.0)
    device = torch.device(args.device)

    records = [json.loads(line) for line in args.dataset.open(encoding="utf-8")]
    rng = random.Random(args.seed)
    rng.shuffle(records)
    n_val = max(1, int(len(records) * args.val_frac))
    val_records, train_records = records[:n_val], records[n_val:]
    print(f"dataset: {len(train_records)} train games, {n_val} val games", flush=True)

    train_chunks = prepare_chunks(train_records, beta=args.shaping_beta, gamma=args.gamma,
                                  batch_size=args.batch_size, seed=args.seed + 1, label="train")
    val_chunks = prepare_chunks(val_records, beta=args.shaping_beta, gamma=args.gamma,
                                batch_size=args.batch_size, seed=args.seed + 2, label="val")

    from nextnation_ai.scripts.train_torch import generate_maps

    probe_maps = generate_maps(args.probe_maps, args.seed + 50000)

    model = NextNationGNNPolicy().to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    ckpt_dir = args.run_dir / "checkpoints"
    log_path = args.run_dir / "bc_log.jsonl"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    best_val_ce = float("inf")

    with log_path.open("a", encoding="utf-8") as log:
        meta = {"type": "metadata", **{k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                "train_games": len(train_records), "val_games": n_val}
        log.write(json.dumps(meta, ensure_ascii=False) + "\n")
        for epoch in range(1, args.epochs + 1):
            started = time.time()
            train_stats = run_epoch(model, train_chunks, device, optimizer=optimizer,
                                    value_coef=args.value_coef, entropy_coef=args.entropy_coef, rng=rng)
            with torch.no_grad():
                val_stats = run_epoch(model, val_chunks, device, optimizer=None,
                                      value_coef=args.value_coef, entropy_coef=args.entropy_coef)
            metrics = {
                "type": "epoch", "epoch": epoch, "elapsed_sec": round(time.time() - started, 1),
                "train": train_stats, "val": val_stats,
            }
            if epoch % args.probe_interval == 0 or epoch == args.epochs:
                model.eval()
                metrics["probe"] = [
                    probe_rollouts(model, device, probe_maps, "random", args.seed + epoch),
                    probe_rollouts(model, device, probe_maps, "idle", args.seed + epoch),
                ]
            save_checkpoint(ckpt_dir / "latest.pt", model, optimizer, metrics)
            if val_stats["ce"] < best_val_ce:
                best_val_ce = val_stats["ce"]
                metrics["best"] = True
                save_checkpoint(ckpt_dir / "best.pt", model, optimizer, metrics)
            log.write(json.dumps(metrics, ensure_ascii=False) + "\n")
            log.flush()
            print(json.dumps(metrics, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()

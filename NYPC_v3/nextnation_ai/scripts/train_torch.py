#!/usr/bin/env python3
"""Train GNN actor-critic with PPO, snapshots, and manual-policy guidance."""

from __future__ import annotations

import argparse
import importlib.util
import json
import multiprocessing as mp
import os
import random
import subprocess
import time
from collections import deque
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import torch

from nextnation_ai.agents.cpp_manual_agent import ManualCppAgent
from nextnation_ai.agents.manual_agent import ManualPythonAgent
from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game import observation as observation_features
from nextnation_ai.game.env import NextNationEnv
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.reward import potential_score
from nextnation_ai.game.state import GameState, Side
from nextnation_ai.nn.torch_policy import (
    ActionComponents,
    NextNationGNNPolicy,
    action_components_from_action,
    evaluate_prepared_batch,
    prepare_evaluation_batch,
    select_action,
)

ROOT = Path(__file__).resolve().parents[2]
TESTING_TOOL = ROOT / "nation-providing" / "testing-tool.py"
DEFAULT_RUN_DIR = ROOT / "runs" / "gnn_ppo"


@dataclass
class Transition:
    state: GameState
    player: Side
    components: ActionComponents
    reward: float
    done: bool
    old_logp: float
    old_value: float
    manual_components: ActionComponents | None
    # Critic value of the state after this transition; set only on the last
    # transition of a truncated (not terminal) trajectory for GAE bootstrap.
    bootstrap_value: float | None = None


@dataclass
class EpisodeStats:
    opponent: str
    side: str
    winner: str
    reason: str | None
    days: int
    reward: float


@dataclass(frozen=True)
class OpponentSpec:
    kind: str
    name: str
    path: str | None = None
    agent_name: str | None = None
    model_state: dict[str, torch.Tensor] | None = None


@dataclass(frozen=True)
class EpisodeJob:
    model_state: dict[str, torch.Tensor]
    opponent: OpponentSpec
    game_map: GameMap
    current_side: Side | None
    seed: int
    manual_path: str
    manual_name: str
    max_days: int
    deterministic: bool
    collect: bool
    shaping_beta: float
    horizon_score_coef: float
    enemy_gold_noise: tuple[float, float, float] | None
    # Query the manual label agent for on-policy DAgger imitation targets.
    # Off by default: on the passive policy's own states the teacher's answer
    # is mostly "do nothing", which anchors the policy to passivity; dataset
    # BC (--bc-dataset) supplies the imitation signal instead (BC_SPEC.md 4).
    dagger_labels: bool = False


def _load_testing_tool_module():
    spec = importlib.util.spec_from_file_location("nextnation_testing_tool", TESTING_TOOL)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot import {TESTING_TOOL}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cpu_state_dict(model: NextNationGNNPolicy) -> dict[str, torch.Tensor]:
    return {name: value.detach().cpu().clone() for name, value in model.state_dict().items()}


def _model_from_state(state: dict[str, torch.Tensor]) -> NextNationGNNPolicy:
    model = NextNationGNNPolicy()
    model.load_state_dict(state)
    model.to(torch.device("cpu"))
    model.eval()
    return model


def _manual_spec(name: str, agent: Any) -> OpponentSpec:
    if isinstance(agent, ManualPythonAgent):
        return OpponentSpec("manual_python", name, str(agent.path), agent.name)
    if isinstance(agent, ManualCppAgent):
        return OpponentSpec("manual_cpp", name, str(agent.path), agent.name)
    raise TypeError(f"unsupported manual opponent: {name} ({type(agent)!r})")


def _opponent_spec(name: str, opponent: Any) -> OpponentSpec:
    if opponent is None:
        return OpponentSpec("self", name)
    if isinstance(opponent, NextNationGNNPolicy):
        return OpponentSpec("snapshot", name, model_state=cpu_state_dict(opponent))
    if isinstance(opponent, RandomLegalAgent):
        return OpponentSpec("random", name)
    return _manual_spec(name, opponent)


def _opponent_from_spec(spec: OpponentSpec, seed: int) -> Any:
    if spec.kind == "self":
        return None
    if spec.kind == "snapshot":
        if spec.model_state is None:
            raise ValueError("snapshot opponent missing model state")
        return _model_from_state(spec.model_state)
    if spec.kind == "random":
        return RandomLegalAgent(seed=seed, upgrade_probability=0.2, move_probability=0.25)
    if spec.kind == "manual_python":
        if spec.path is None:
            raise ValueError("manual python opponent missing path")
        return ManualPythonAgent(Path(spec.path), name=spec.agent_name or Path(spec.path).stem)
    if spec.kind == "manual_cpp":
        if spec.path is None:
            raise ValueError("manual C++ opponent missing path")
        return ManualCppAgent(Path(spec.path), name=spec.agent_name)
    raise ValueError(f"unknown opponent kind: {spec.kind}")


def _init_rollout_worker(torch_threads: int) -> None:
    torch.set_num_threads(max(1, int(torch_threads)))


def run_episode_job(job: EpisodeJob) -> tuple[list[list[Transition]], EpisodeStats]:
    random.seed(job.seed)
    torch.manual_seed(job.seed)
    observation_features.ENEMY_GOLD_NOISE = job.enemy_gold_noise
    device = torch.device("cpu")
    model = _model_from_state(job.model_state)
    opponent = _opponent_from_spec(job.opponent, job.seed + 1000003)
    if opponent is None:
        opponent = model
    manual = ManualPythonAgent(Path(job.manual_path), name=job.manual_name) if job.dagger_labels else None
    return play_episode(
        model,
        opponent,
        job.opponent.name,
        job.game_map,
        current_side=job.current_side,
        manual_agent=manual,
        device=device,
        max_days=job.max_days,
        deterministic=job.deterministic,
        collect=job.collect,
        shaping_beta=job.shaping_beta,
        horizon_score_coef=job.horizon_score_coef,
    )


def run_episode_jobs(
    jobs: list[EpisodeJob],
    executor: ProcessPoolExecutor | None,
) -> tuple[list[list[Transition]], list[EpisodeStats]]:
    trajectories: list[list[Transition]] = []
    stats: list[EpisodeStats] = []
    if executor is None:
        results = [run_episode_job(job) for job in jobs]
    else:
        results = list(executor.map(run_episode_job, jobs))
    for trajs, st in results:
        trajectories.extend(trajs)
        stats.append(st)
    return trajectories, stats


def generate_maps(count: int, seed: int, *, np_: int = 25, kp: int = 4) -> list[GameMap]:
    module = _load_testing_tool_module()
    maps: list[GameMap] = []
    for i in range(count):
        rows = module.generate_map(module.XoShiro256(seed + i), NP=np_, KP=kp)
        maps.append(GameMap.from_lines(rows))
    return maps


def winner_name(winner: Side | None) -> str:
    if winner is Side.LEFT:
        return "LEFT"
    if winner is Side.RIGHT:
        return "RIGHT"
    return "DRAW"


def opponent_act(opponent: Any, state: GameState, player: Side, device: torch.device):
    if isinstance(opponent, NextNationGNNPolicy):
        with torch.no_grad():
            return select_action(opponent, state, player, device, deterministic=True).action
    try:
        return opponent.act(state, player)
    except Exception:
        return None


def manual_components(manual_agent: ManualPythonAgent, state: GameState, player: Side) -> ActionComponents | None:
    try:
        action = manual_agent.act(state, player)
        return action_components_from_action(state, player, action)
    except Exception:
        return None


def play_episode(
    model: NextNationGNNPolicy,
    opponent: Any,
    opponent_name: str,
    game_map: GameMap,
    *,
    current_side: Side | None,
    manual_agent: ManualPythonAgent | None,
    device: torch.device,
    max_days: int,
    deterministic: bool,
    collect: bool,
    shaping_beta: float,
    horizon_score_coef: float,
) -> tuple[list[list[Transition]], EpisodeStats]:
    from nextnation_ai.game.actions import CommandAction

    env = NextNationEnv(game_map, shaping_beta=shaping_beta)
    env.reset(game_map)
    controlled = {Side.LEFT, Side.RIGHT} if current_side is None else {current_side}
    trajectories: dict[Side, list[Transition]] = {Side.LEFT: [], Side.RIGHT: []}
    total_reward = 0.0
    external_sides: list[Side] = []

    if hasattr(opponent, "start_episode"):
        for side in (Side.LEFT, Side.RIGHT):
            if side not in controlled:
                try:
                    opponent.start_episode(game_map, side)
                    external_sides.append(side)
                except Exception:
                    external_sides.clear()
                break

    try:
        while not env.state.done and env.state.day < max_days:
            before = env.state.clone()
            decisions = {}
            if Side.LEFT in controlled:
                decisions[Side.LEFT] = select_action(model, before, Side.LEFT, device, deterministic=deterministic)
                left_action = decisions[Side.LEFT].action
            else:
                left_action = opponent_act(opponent, before, Side.LEFT, device) or CommandAction()
            if Side.RIGHT in controlled:
                decisions[Side.RIGHT] = select_action(model, before, Side.RIGHT, device, deterministic=deterministic)
                right_action = decisions[Side.RIGHT].action
            else:
                right_action = opponent_act(opponent, before, Side.RIGHT, device) or CommandAction()

            step = env.step(left_action, right_action)
            for ext_side in external_sides:
                try:
                    opponent.observe(env.state, ext_side, step.info["events"])
                except Exception:
                    pass
            rewards = {Side.LEFT: step.reward_left, Side.RIGHT: step.reward_right}
            for side, decision in decisions.items():
                total_reward += rewards[side]
                if collect:
                    trajectories[side].append(
                        Transition(
                            state=before.clone(),
                            player=side,
                            components=decision.components,
                            reward=float(rewards[side]),
                            done=step.done,
                            old_logp=float(decision.logp.detach().cpu()),
                            old_value=float(decision.value.detach().cpu()),
                            manual_components=(
                                manual_components(manual_agent, before, side) if manual_agent is not None else None
                            ),
                        )
                    )
    finally:
        if hasattr(opponent, "finish"):
            try:
                opponent.finish()
            except Exception:
                pass

    if collect and not env.state.done:
        if horizon_score_coef != 0.0:
            # Legacy pseudo-terminal: replace the bootstrap with a potential bonus.
            for side in controlled:
                if trajectories[side]:
                    bonus = horizon_score_coef * potential_score(env.state, side)
                    trajectories[side][-1].reward += float(bonus)
                    trajectories[side][-1].done = True
                    total_reward += float(bonus)
        else:
            # Truncated episode: bootstrap GAE with the critic's value of the
            # final state instead of 0 (FIX_SPEC.md section 2b).
            for side in controlled:
                if trajectories[side]:
                    with torch.no_grad():
                        tail = select_action(model, env.state, side, device, deterministic=True)
                    trajectories[side][-1].bootstrap_value = float(tail.value.detach().cpu())

    stats = EpisodeStats(
        opponent=opponent_name,
        side="both" if current_side is None else current_side.word,
        winner=winner_name(env.state.winner),
        reason=env.state.terminal_reason,
        days=env.state.day,
        reward=total_reward,
    )
    return [traj for side, traj in trajectories.items() if side in controlled and traj], stats


def lerp(start: float, end: float, progress: float) -> float:
    progress = max(0.0, min(1.0, progress))
    return start + (end - start) * progress


def choose_opponent(
    rng: random.Random,
    manual_pool: list[tuple[str, Any]],
    random_agent: RandomLegalAgent,
    snapshots: list[NextNationGNNPolicy],
    *,
    manual_weight: float,
    random_weight: float,
    self_weight: float,
    snapshot_weight: float,
):
    choices: list[tuple[float, str, Any, Side | None]] = []
    if manual_pool and manual_weight > 0.0:
        each = manual_weight / len(manual_pool)
        for name, agent in manual_pool:
            choices.append((each, name, agent, rng.choice([Side.LEFT, Side.RIGHT])))
    choices.extend([
        (random_weight, "random", random_agent, rng.choice([Side.LEFT, Side.RIGHT])),
        (self_weight, "self", None, None),
    ])
    if snapshots:
        choices.append((snapshot_weight, "snapshot", rng.choice(snapshots), rng.choice([Side.LEFT, Side.RIGHT])))
    choices = [choice for choice in choices if choice[0] > 0.0]
    total = sum(c[0] for c in choices)
    if total <= 0.0:
        name, agent = manual_pool[0]
        return name, agent, Side.LEFT
    r = rng.random() * total
    acc = 0.0
    for weight, name, opp, side in choices:
        acc += weight
        if r <= acc:
            return name, opp, side
    return choices[-1][1], choices[-1][2], choices[-1][3]


def compute_returns_advantages(trajectories: list[list[Transition]], gamma: float, gae_lambda: float):
    flat: list[Transition] = []
    returns: list[float] = []
    advantages: list[float] = []
    for traj in trajectories:
        gae = 0.0
        next_value = 0.0
        if traj and traj[-1].bootstrap_value is not None and not traj[-1].done:
            next_value = traj[-1].bootstrap_value
        traj_returns = [0.0] * len(traj)
        traj_adv = [0.0] * len(traj)
        for i in reversed(range(len(traj))):
            tr = traj[i]
            not_done = 0.0 if tr.done else 1.0
            delta = tr.reward + gamma * next_value * not_done - tr.old_value
            gae = delta + gamma * gae_lambda * not_done * gae
            traj_adv[i] = gae
            traj_returns[i] = gae + tr.old_value
            next_value = tr.old_value
        flat.extend(traj)
        returns.extend(traj_returns)
        advantages.extend(traj_adv)
    if not flat:
        return flat, torch.empty(0), torch.empty(0)
    adv = torch.tensor(advantages, dtype=torch.float32)
    adv = (adv - adv.mean()) / adv.std(unbiased=False).clamp_min(1e-8)
    ret = torch.tensor(returns, dtype=torch.float32)
    return flat, ret, adv


def ppo_update(
    model: NextNationGNNPolicy,
    optimizer: torch.optim.Optimizer,
    trajectories: list[list[Transition]],
    device: torch.device,
    *,
    gamma: float,
    gae_lambda: float,
    epochs: int,
    clip_eps: float,
    value_coef: float,
    entropy_coef: float,
    manual_bc_coef: float,
    max_grad_norm: float,
    microbatch: int = 1024,
    bc_batches: list[Any] | None = None,
):
    transitions, returns_cpu, adv_cpu = compute_returns_advantages(trajectories, gamma, gae_lambda)
    if not transitions:
        return {"samples": 0}
    total = len(transitions)
    returns = returns_cpu.to(device)
    advantages = adv_cpu.to(device)
    old_logp = torch.tensor([t.old_logp for t in transitions], dtype=torch.float32, device=device)

    # Observations, legal masks, chosen indices, and imitation targets depend
    # only on the stored transitions, so they are prepared once and reused
    # across epochs; each epoch is then batched forwards/backwards only.
    prep_start = time.time()
    chunk_size = total if microbatch <= 0 else max(1, microbatch)
    chunks: list[tuple[Any, int, int]] = []
    for lo in range(0, total, chunk_size):
        sub = transitions[lo : lo + chunk_size]
        items = [(tr.state, tr.player, tr.components, tr.manual_components) for tr in sub]
        chunks.append((prepare_evaluation_batch(items, device), lo, lo + len(sub)))
    prepare_sec = time.time() - prep_start
    # Teacher-dataset BC batches contribute imitation CE only (no policy /
    # value / entropy gradient) and share the imitation normalization with the
    # rollout labels (BC_SPEC.md section 4).
    bc_batches = bc_batches or []
    total_imit = int(sum(float(batch.im_present.sum()) for batch, _, _ in chunks))
    total_imit += int(sum(float(batch.im_present.sum()) for batch in bc_batches))

    last_stats: dict[str, float] = {}
    for _ in range(epochs):
        optimizer.zero_grad(set_to_none=True)
        policy_sum = value_sq_sum = entropy_sum = imit_sum = kl_sum = clip_count = 0.0
        for batch, lo, hi in chunks:
            ev = evaluate_prepared_batch(model, batch)
            ratio = torch.exp(ev.logp - old_logp[lo:hi])
            unclipped = ratio * advantages[lo:hi]
            clipped = torch.clamp(ratio, 1.0 - clip_eps, 1.0 + clip_eps) * advantages[lo:hi]
            chunk_policy_sum = -torch.min(unclipped, clipped).sum()
            chunk_value_sq_sum = ((ev.value - returns[lo:hi]) ** 2).sum()
            chunk_entropy_sum = ev.entropy.sum()
            chunk_imit_sum = ev.imitation_per_item.sum()

            # Per-chunk share of the full-batch loss; gradient accumulation
            # over chunks reproduces the exact full-batch gradient.
            chunk_loss = (chunk_policy_sum + value_coef * chunk_value_sq_sum - entropy_coef * chunk_entropy_sum) / total
            if total_imit > 0:
                chunk_loss = chunk_loss + manual_bc_coef * chunk_imit_sum / total_imit
            chunk_loss.backward()

            with torch.no_grad():
                policy_sum += float(chunk_policy_sum)
                value_sq_sum += float(chunk_value_sq_sum)
                entropy_sum += float(chunk_entropy_sum)
                imit_sum += float(chunk_imit_sum)
                kl_sum += float((old_logp[lo:hi] - ev.logp).sum())
                clip_count += float(((ratio - 1.0).abs() > clip_eps).float().sum())
        for batch in bc_batches:
            ev = evaluate_prepared_batch(model, batch)
            bc_imit_sum = ev.imitation_per_item.sum()
            if total_imit > 0 and manual_bc_coef > 0.0:
                (manual_bc_coef * bc_imit_sum / total_imit).backward()
            imit_sum += float(bc_imit_sum.detach())
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_grad_norm)
        optimizer.step()

        policy_loss = policy_sum / total
        value_loss = value_sq_sum / total
        entropy = entropy_sum / total
        imitation_loss = imit_sum / total_imit if total_imit > 0 else 0.0
        last_stats = {
            "samples": total,
            "loss": policy_loss + value_coef * value_loss - entropy_coef * entropy + manual_bc_coef * imitation_loss,
            "policy_loss": policy_loss,
            "value_loss": value_loss,
            "entropy": entropy,
            "imitation_loss": imitation_loss,
            "approx_kl": kl_sum / total,
            "clip_fraction": clip_count / total,
            "avg_return": float(returns.mean().detach().cpu()),
            "prepare_sec": round(prepare_sec, 3),
            "microbatches": len(chunks),
        }
    return last_stats


def eval_score(summary: dict[str, Any]) -> float:
    if not summary:
        return float("-inf")
    return sum(float(rec.get("avg_reward", 0.0)) for rec in summary.values()) / max(1, len(summary))


def summarize(stats: list[EpisodeStats]) -> dict[str, Any]:
    out: dict[str, Any] = {}
    for st in stats:
        rec = out.setdefault(st.opponent, {"games": 0, "wins": 0, "losses": 0, "draws": 0, "avg_days": 0.0, "avg_reward": 0.0})
        rec["games"] += 1
        rec["avg_days"] += st.days
        rec["avg_reward"] += st.reward
        if st.winner == "DRAW" or st.side == "both":
            rec["draws"] += 1
        elif st.winner == st.side:
            rec["wins"] += 1
        else:
            rec["losses"] += 1
    for rec in out.values():
        rec["avg_days"] /= max(1, rec["games"])
        rec["avg_reward"] /= max(1, rec["games"])
        rec["win_rate"] = rec["wins"] / max(1, rec["games"])
    return out


def make_episode_job(
    model_state: dict[str, torch.Tensor],
    opponent: OpponentSpec,
    game_map: GameMap,
    current_side: Side | None,
    *,
    seed: int,
    manual_agent: ManualPythonAgent,
    max_days: int,
    deterministic: bool,
    collect: bool,
    shaping_beta: float,
    horizon_score_coef: float,
    enemy_gold_noise: tuple[float, float, float] | None,
    dagger_labels: bool = False,
) -> EpisodeJob:
    return EpisodeJob(
        model_state=model_state,
        opponent=opponent,
        game_map=game_map,
        current_side=current_side,
        seed=seed,
        manual_path=str(manual_agent.path),
        manual_name=manual_agent.name,
        max_days=max_days,
        deterministic=deterministic,
        collect=collect,
        shaping_beta=shaping_beta,
        horizon_score_coef=horizon_score_coef,
        enemy_gold_noise=enemy_gold_noise,
        dagger_labels=dagger_labels,
    )


def evaluate(
    model,
    maps,
    manual_label_agent,
    manual_pool,
    random_agent,
    snapshots,
    device,
    max_days,
    shaping_beta: float,
    *,
    executor: ProcessPoolExecutor | None = None,
    enemy_gold_noise: tuple[float, float, float] | None = None,
    base_seed: int = 0,
):
    del device
    model.eval()
    opponents: list[tuple[str, Any]] = [*manual_pool, ("random", random_agent)]
    if snapshots:
        opponents.append(("latest_snapshot", snapshots[-1]))
    model_state = cpu_state_dict(model)
    jobs: list[EpisodeJob] = []
    for opp_index, (name, opp) in enumerate(opponents):
        spec = _opponent_spec(name, opp)
        for map_index, game_map in enumerate(maps[:2]):
            for side_index, side in enumerate((Side.LEFT, Side.RIGHT)):
                jobs.append(
                    make_episode_job(
                        model_state,
                        spec,
                        game_map,
                        side,
                        seed=base_seed + opp_index * 1000 + map_index * 10 + side_index,
                        manual_agent=manual_label_agent,
                        max_days=max_days,
                        deterministic=True,
                        collect=False,
                        shaping_beta=shaping_beta,
                        horizon_score_coef=0.0,
                        enemy_gold_noise=enemy_gold_noise,
                    )
                )
    _, stats = run_episode_jobs(jobs, executor)
    model.train()
    return summarize(stats)


def compile_manual_cpp(build_dir: Path) -> dict[str, str]:
    build_dir.mkdir(parents=True, exist_ok=True)
    result = {}
    for src in sorted((ROOT / "manual_model").glob("*.cpp")):
        out = build_dir / src.stem
        proc = subprocess.run(["g++", "-O2", "-std=c++17", str(src), "-o", str(out)], cwd=ROOT, text=True, capture_output=True)
        result[src.name] = "ok" if proc.returncode == 0 else (proc.stderr.strip() or "compile failed")
    return result


def build_manual_pool(manual_label_agent: ManualPythonAgent, cpp_status: dict[str, str], cpp_dir: Path) -> list[tuple[str, Any]]:
    pool: list[tuple[str, Any]] = [(f"manual:{manual_label_agent.name}", manual_label_agent)]
    for src_name, status in sorted(cpp_status.items()):
        if status != "ok":
            continue
        stem = Path(src_name).stem
        exe = cpp_dir / stem
        if exe.exists():
            pool.append((f"manual:{stem}", ManualCppAgent(exe, name=stem)))
    return pool


def build_bc_chunks(
    dataset: Path,
    *,
    pool_items: int,
    microbatch: int,
    beta: float,
    gamma: float,
    seed: int,
    device: torch.device,
) -> list[Any]:
    """Prepare teacher-dataset imitation chunks once; sampled every update."""
    from nextnation_ai.scripts.pretrain_bc import replay_game_items

    records = [json.loads(line) for line in dataset.open(encoding="utf-8")]
    items = [item for record in records for item, _ret in replay_game_items(record, beta, gamma)]
    rng = random.Random(seed)
    if 0 < pool_items < len(items):
        items = rng.sample(items, pool_items)
    else:
        rng.shuffle(items)
    chunk_size = max(1, microbatch)
    return [
        prepare_evaluation_batch(items[lo : lo + chunk_size], device)
        for lo in range(0, len(items), chunk_size)
    ]


def save_checkpoint(path: Path, model, optimizer, update: int, metrics: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"update": update, "model_state": model.state_dict(), "optimizer_state": optimizer.state_dict(), "metrics": metrics}, path)


def load_policy(path: Path, device: torch.device) -> NextNationGNNPolicy:
    ckpt = torch.load(path, map_location=device)
    model = NextNationGNNPolicy()
    model.load_state_dict(ckpt["model_state"])
    model.to(device)
    model.eval()
    return model


def load_checkpoint_into(path: Path, model: NextNationGNNPolicy, optimizer: torch.optim.Optimizer | None, device: torch.device, *, optimizer_state: bool = False) -> int:
    ckpt = torch.load(path, map_location=device)
    model.load_state_dict(ckpt["model_state"])
    if optimizer_state and optimizer is not None and "optimizer_state" in ckpt:
        optimizer.load_state_dict(ckpt["optimizer_state"])
    return int(ckpt.get("update", 0))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--updates", type=int, default=20)
    parser.add_argument("--episodes-per-update", type=int, default=4)
    parser.add_argument("--max-days", type=int, default=80)
    parser.add_argument("--eval-days", type=int, default=120)
    parser.add_argument("--map-count", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20260701)
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--gamma", type=float, default=0.997)
    parser.add_argument("--gae-lambda", type=float, default=0.95)
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--ppo-microbatch", type=int, default=1024,
                        help="Transitions per batched PPO forward/backward; 0 = one full batch.")
    parser.add_argument("--clip-eps", type=float, default=0.2)
    parser.add_argument("--entropy-coef", type=float, default=0.015)
    parser.add_argument("--value-coef", type=float, default=0.5)
    parser.add_argument("--manual-bc-coef", type=float, default=0.05)
    parser.add_argument("--manual-bc-start", type=float, default=0.25)
    parser.add_argument("--manual-bc-end", type=float, default=0.12)
    parser.add_argument("--manual-opponent-start", type=float, default=0.65)
    parser.add_argument("--manual-opponent-end", type=float, default=0.40)
    parser.add_argument("--manual-train-include", type=str, default=None,
                        help="Comma-separated manual-bot stems to use as TRAINING opponents (curriculum). "
                             "Eval always uses the full pool. Unset = all bots train.")
    parser.add_argument("--random-opponent-weight", type=float, default=0.15)
    parser.add_argument("--self-opponent-weight", type=float, default=0.20)
    parser.add_argument("--snapshot-opponent-weight", type=float, default=0.30)
    parser.add_argument("--shaping-beta-start", type=float, default=0.20)
    parser.add_argument("--shaping-beta-end", type=float, default=0.03)
    parser.add_argument("--shaping-beta", type=float, default=None,
                        help="Backward-compatible fixed shaping beta; sets both start and end.")
    parser.add_argument("--horizon-score-coef", type=float, default=0.0,
                        help="If nonzero, truncated episodes end with this * potential instead of a critic bootstrap.")
    parser.add_argument("--enemy-gold-noise", action=argparse.BooleanOptionalAction, default=True,
                        help="Train with a noised enemy-gold feature to match the inference-time estimate (FIX_SPEC 3a).")
    parser.add_argument("--bc-dataset", type=Path, default=None,
                        help="Teacher games JSONL from collect_bc_games.py; mixes dataset-BC imitation chunks into every update (BC_SPEC 4).")
    parser.add_argument("--bc-pool-items", type=int, default=8192,
                        help="Max dataset items kept in the prepared BC pool (0 = all).")
    parser.add_argument("--bc-chunks-per-update", type=int, default=2,
                        help="Prepared BC chunks (of --ppo-microbatch items) sampled per update.")
    parser.add_argument("--dagger-labels", action=argparse.BooleanOptionalAction, default=False,
                        help="Legacy on-policy imitation labels from lth-v4-4; on the policy's own degenerate states these are mostly empty and anchor it to passivity.")
    parser.add_argument("--manual-gate-winrate", type=float, default=0.5,
                        help="Hold the opponent-mix/BC/beta annealing at start values until the rolling train win rate vs manual opponents reaches this (0 disables).")
    parser.add_argument("--manual-gate-window", type=int, default=100,
                        help="Rolling window (train games vs manual opponents) for the gate.")
    parser.add_argument("--init-checkpoint", type=Path)
    parser.add_argument("--resume-optimizer", action="store_true")
    parser.add_argument("--checkpoint-interval", type=int, default=5)
    parser.add_argument("--snapshot-interval", type=int, default=5)
    parser.add_argument("--eval-interval", type=int, default=5)
    parser.add_argument("--run-dir", type=Path, default=DEFAULT_RUN_DIR)
    parser.add_argument("--compile-manual-cpp", action="store_true")
    parser.add_argument("--rollout-workers", type=int, default=0,
                        help="CPU worker processes for rollout/eval. 0 uses min(episodes_per_update, cpu_count).")
    parser.add_argument("--worker-torch-threads", type=int, default=1,
                        help="Torch CPU threads per rollout worker process.")
    args = parser.parse_args()

    if args.shaping_beta is not None:
        args.shaping_beta_start = args.shaping_beta
        args.shaping_beta_end = args.shaping_beta

    rng = random.Random(args.seed)
    torch.manual_seed(args.seed)
    device = torch.device(args.device)
    if args.enemy_gold_noise:
        observation_features.ENEMY_GOLD_NOISE = (0.8, 1.3, 100.0)
    args.run_dir.mkdir(parents=True, exist_ok=True)
    ckpt_dir = args.run_dir / "checkpoints"
    log_path = args.run_dir / "train_log.jsonl"

    train_maps = generate_maps(args.map_count, args.seed)
    eval_maps = generate_maps(max(2, min(4, args.map_count)), args.seed + 10000)
    manual = ManualPythonAgent()
    cpp_dir = args.run_dir / "manual_bins"
    cpp_status = compile_manual_cpp(cpp_dir)
    manual_pool = build_manual_pool(manual, cpp_status, cpp_dir)
    # Eval always uses the full pool; training may use a beatable subset so the
    # policy gets a winnable curriculum instead of only unbeatable opponents.
    if args.manual_train_include:
        include = {s.strip() for s in args.manual_train_include.split(",") if s.strip()}
        manual_train_pool = [(n, a) for (n, a) in manual_pool if n.split("manual:")[-1] in include]
        if not manual_train_pool:
            raise SystemExit(f"--manual-train-include matched no bots; pool = {[n for n, _ in manual_pool]}")
    else:
        manual_train_pool = manual_pool
    random_agent = RandomLegalAgent(seed=args.seed + 1, upgrade_probability=0.2, move_probability=0.25)
    model = NextNationGNNPolicy().to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr)
    init_update = 0
    if args.init_checkpoint is not None:
        init_update = load_checkpoint_into(args.init_checkpoint, model, optimizer, device, optimizer_state=args.resume_optimizer)
    bc_chunks: list[Any] = []
    if args.bc_dataset is not None:
        bc_chunks = build_bc_chunks(
            args.bc_dataset,
            pool_items=args.bc_pool_items,
            microbatch=args.ppo_microbatch,
            beta=args.shaping_beta_start,
            gamma=args.gamma,
            seed=args.seed + 777,
            device=device,
        )
        print(f"bc dataset: {len(bc_chunks)} prepared chunks of <= {args.ppo_microbatch} items", flush=True)
    snapshots: list[NextNationGNNPolicy] = []
    auto_workers = min(max(1, args.episodes_per_update), os.cpu_count() or 1)
    rollout_workers = auto_workers if args.rollout_workers <= 0 else max(1, args.rollout_workers)
    enemy_gold_noise = observation_features.ENEMY_GOLD_NOISE if args.enemy_gold_noise else None

    metadata = {
        "type": "metadata",
        "torch": torch.__version__,
        "torch_cuda": torch.version.cuda,
        "cuda_available": torch.cuda.is_available(),
        "device": str(device),
        "manual_python": str(manual.path),
        "manual_opponents": [name for name, _ in manual_pool],
        "manual_train_opponents": [name for name, _ in manual_train_pool],
        "updates": args.updates,
        "episodes_per_update": args.episodes_per_update,
        "manual_bc_coef": args.manual_bc_coef,
        "manual_bc_start": args.manual_bc_start,
        "manual_bc_end": args.manual_bc_end,
        "manual_opponent_start": args.manual_opponent_start,
        "manual_opponent_end": args.manual_opponent_end,
        "shaping_beta_start": args.shaping_beta_start,
        "shaping_beta_end": args.shaping_beta_end,
        "horizon_score_coef": args.horizon_score_coef,
        "enemy_gold_noise": args.enemy_gold_noise,
        "ppo_microbatch": args.ppo_microbatch,
        "rollout_workers": rollout_workers,
        "worker_torch_threads": args.worker_torch_threads,
        "rollout_device": "cpu",
        "update_device": str(device),
        "init_checkpoint": None if args.init_checkpoint is None else str(args.init_checkpoint),
        "init_update": init_update,
        "bc_dataset": None if args.bc_dataset is None else str(args.bc_dataset),
        "bc_pool_chunks": len(bc_chunks),
        "bc_chunks_per_update": args.bc_chunks_per_update,
        "dagger_labels": args.dagger_labels,
        "manual_gate_winrate": args.manual_gate_winrate,
        "manual_gate_window": args.manual_gate_window,
    }
    metadata["manual_cpp_compile"] = cpp_status

    rollout_executor: ProcessPoolExecutor | None = None
    if rollout_workers > 1:
        ctx = mp.get_context("spawn")
        rollout_executor = ProcessPoolExecutor(
            max_workers=rollout_workers,
            mp_context=ctx,
            initializer=_init_rollout_worker,
            initargs=(args.worker_torch_threads,),
        )

    try:
        with log_path.open("a", encoding="utf-8") as log:
            log.write(json.dumps(metadata, ensure_ascii=False) + "\n")
            last_eval: dict[str, Any] = {}
            best_eval_score = float("-inf")
            # Competence gate: schedules hold at their start values until the
            # rolling train win rate vs manual opponents clears the threshold,
            # then anneal over the remaining updates (BC_SPEC.md section 4).
            manual_results: deque[int] = deque(maxlen=max(1, args.manual_gate_window))
            gate_passed = args.manual_gate_winrate <= 0.0
            gate_update = 0
            for update in range(1, args.updates + 1):
                started = time.time()
                model.train()
                if gate_passed:
                    progress = (update - 1 - gate_update) / max(1, args.updates - 1 - gate_update)
                else:
                    progress = 0.0
                manual_weight = lerp(args.manual_opponent_start, args.manual_opponent_end, progress)
                manual_bc_coef = lerp(args.manual_bc_start, args.manual_bc_end, progress)
                shaping_beta = lerp(args.shaping_beta_start, args.shaping_beta_end, progress)
                model_state = cpu_state_dict(model)
                jobs: list[EpisodeJob] = []
                for episode_index in range(args.episodes_per_update):
                    name, opp, side = choose_opponent(
                        rng,
                        manual_train_pool,
                        random_agent,
                        snapshots,
                        manual_weight=manual_weight,
                        random_weight=args.random_opponent_weight,
                        self_weight=args.self_opponent_weight,
                        snapshot_weight=args.snapshot_opponent_weight,
                    )
                    jobs.append(
                        make_episode_job(
                            model_state,
                            _opponent_spec(name, opp),
                            rng.choice(train_maps),
                            side,
                            seed=args.seed + update * 10000 + episode_index,
                            manual_agent=manual,
                            max_days=args.max_days,
                            deterministic=False,
                            collect=True,
                            shaping_beta=shaping_beta,
                            horizon_score_coef=args.horizon_score_coef,
                            enemy_gold_noise=enemy_gold_noise,
                            dagger_labels=args.dagger_labels,
                        )
                    )
                trajectories, episode_stats = run_episode_jobs(jobs, rollout_executor)
                for st in episode_stats:
                    if st.opponent.startswith("manual:") and st.side != "both":
                        manual_results.append(1 if st.winner == st.side else 0)
                manual_winrate = sum(manual_results) / len(manual_results) if manual_results else 0.0
                if (
                    not gate_passed
                    and len(manual_results) >= min(args.manual_gate_window, 30)
                    and manual_winrate >= args.manual_gate_winrate
                ):
                    gate_passed = True
                    gate_update = update
                bc_batches: list[Any] = []
                if bc_chunks:
                    k = min(args.bc_chunks_per_update, len(bc_chunks))
                    bc_batches = [bc_chunks[i] for i in rng.sample(range(len(bc_chunks)), k)]
                train_stats = ppo_update(
                    model,
                    optimizer,
                    trajectories,
                    device,
                    gamma=args.gamma,
                    gae_lambda=args.gae_lambda,
                    epochs=args.epochs,
                    clip_eps=args.clip_eps,
                    value_coef=args.value_coef,
                    entropy_coef=args.entropy_coef,
                    manual_bc_coef=manual_bc_coef,
                    max_grad_norm=0.5,
                    microbatch=args.ppo_microbatch,
                    bc_batches=bc_batches,
                )
                if update % args.snapshot_interval == 0:
                    snap = NextNationGNNPolicy().to(device)
                    snap.load_state_dict(model.state_dict())
                    snap.eval()
                    snapshots.append(snap)
                    snapshots[:] = snapshots[-8:]
                if update % args.eval_interval == 0 or update == 1:
                    # Evaluate with a fixed beta so eval_score/best-checkpoint
                    # selection stays comparable across updates despite annealing.
                    last_eval = evaluate(
                        model,
                        eval_maps,
                        manual,
                        manual_pool,
                        random_agent,
                        snapshots,
                        device,
                        args.eval_days,
                        args.shaping_beta_end,
                        executor=rollout_executor,
                        enemy_gold_noise=enemy_gold_noise,
                        base_seed=args.seed + update * 100000,
                    )
                current_eval_score = eval_score(last_eval)
                metrics = {
                    "type": "update",
                    "update": update,
                    "elapsed_sec": round(time.time() - started, 3),
                    "train": train_stats,
                    "train_opponents": summarize(episode_stats),
                    "snapshots": len(snapshots),
                    "manual_weight": manual_weight,
                    "manual_bc_coef": manual_bc_coef,
                    "shaping_beta": shaping_beta,
                    "manual_winrate_window": round(manual_winrate, 4),
                    "gate_passed": gate_passed,
                    "gate_update": gate_update,
                    "eval_score": current_eval_score,
                    "eval": last_eval,
                }
                if current_eval_score > best_eval_score:
                    best_eval_score = current_eval_score
                    best_path = ckpt_dir / "best.pt"
                    save_checkpoint(best_path, model, optimizer, update, metrics)
                    metrics["best_checkpoint"] = str(best_path)
                if update % args.checkpoint_interval == 0 or update == 1 or update == args.updates:
                    ckpt = ckpt_dir / f"policy_update_{update:05d}.pt"
                    save_checkpoint(ckpt, model, optimizer, update, metrics)
                    save_checkpoint(ckpt_dir / "latest.pt", model, optimizer, update, metrics)
                    metrics["checkpoint"] = str(ckpt)
                log.write(json.dumps(metrics, ensure_ascii=False) + "\n")
                log.flush()
                print(json.dumps(metrics, ensure_ascii=False), flush=True)
    finally:
        if rollout_executor is not None:
            rollout_executor.shutdown()


if __name__ == "__main__":
    main()

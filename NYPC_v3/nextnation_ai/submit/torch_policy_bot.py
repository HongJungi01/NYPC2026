#!/usr/bin/env python3
"""Interactive bot backed by a GNN PPO checkpoint."""

from __future__ import annotations

import argparse
import multiprocessing as mp
import queue
import sys
import time
from typing import Any

from nextnation_ai.game.actions import CommandAction, action_to_commands
from nextnation_ai.game.constants import BASE_BUILD_COST, FOOD_COST, HQ_TABLE, INCOME_PER_WORKER, MOVE_COST, TRAIN_COST
from nextnation_ai.game.legal import move_command_cost, validate_action
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.state import Building, BuildingKind, Side, Unit, UnitId, create_initial_state


def readln() -> str:
    line = sys.stdin.readline()
    if not line:
        raise SystemExit(0)
    return line.rstrip("\n")


def read_tokens() -> list[str]:
    return readln().split()


def parse_ready():
    first = read_tokens()
    if len(first) != 2 or first[0] != "READY":
        raise SystemExit("expected READY")
    side = Side.from_word(first[1])
    n, k = (int(v) for v in read_tokens())
    x = [int(v) for v in read_tokens()]
    y = [int(v) for v in read_tokens()]
    points = [int(v) for v in read_tokens()]
    adj = []
    for _ in range(n):
        row = [int(v) for v in read_tokens()]
        adj.append(row[1:])
    game_map = GameMap(n, k, x, y, points, adj)
    return side, create_initial_state(game_map)


def load_model(path: str, requested_device: str) -> tuple[Any, Any, Any, Any]:
    import torch

    from nextnation_ai.nn.torch_policy import NextNationGNNPolicy, select_action

    # Contest execution is latency-sensitive and CPU is more stable than CUDA
    # startup/first-kernel latency for this model. Keep the CLI argument for
    # backward compatibility, but always run inference on CPU here.
    device = torch.device("cpu")
    ckpt = torch.load(path, map_location=device)
    model = NextNationGNNPolicy().to(device)
    model.load_state_dict(ckpt["model_state"])
    model.eval()
    return torch, device, model, select_action


def policy_worker(
    checkpoint: str,
    requested_device: str,
    requests: Any,
    responses: Any,
    warm_state: Any | None = None,
    warm_side: Side | None = None,
) -> None:
    try:
        torch_mod, device, model, select_fn = load_model(checkpoint, requested_device)
        if warm_state is not None and warm_side is not None:
            with torch_mod.no_grad():
                select_fn(model, warm_state, warm_side, device, deterministic=True)
        responses.put(("ready", None, None))
        while True:
            item = requests.get()
            if item is None:
                return
            req_id, state, side = item
            with torch_mod.no_grad():
                decision = select_fn(model, state, side, device, deterministic=True)
            action = decision.action if validate_action(state, side, decision.action) else CommandAction()
            responses.put(("action", req_id, action))
    except BaseException as exc:
        responses.put(("error", None, f"{type(exc).__name__}: {exc}"))


def start_policy_worker(
    checkpoint: str,
    requested_device: str,
    warm_state: Any | None = None,
    warm_side: Side | None = None,
) -> dict[str, Any]:
    ctx = mp.get_context("fork") if "fork" in mp.get_all_start_methods() else mp.get_context()
    requests = ctx.Queue(maxsize=1)
    responses = ctx.Queue()
    proc = ctx.Process(
        target=policy_worker,
        args=(checkpoint, requested_device, requests, responses, warm_state.clone() if warm_state is not None else None, warm_side),
        daemon=True,
    )
    proc.start()
    return {
        "proc": proc,
        "requests": requests,
        "responses": responses,
        "ready": False,
        "error": None,
        "next_req_id": 0,
        "pending_req_id": None,
    }


def handle_worker_message(worker: dict[str, Any], kind: str, req_id: int | None, payload: Any) -> None:
    if kind == "ready":
        worker["ready"] = True
    elif kind == "error":
        worker["error"] = payload
        worker["ready"] = False
        worker["pending_req_id"] = None
        if not worker.get("error_reported"):
            print(f"policy worker error: {payload}", file=sys.stderr, flush=True)
            worker["error_reported"] = True
    elif kind == "action":
        # Stale actions are ignored; each turn asks using the current state.
        worker["last_action"] = (req_id, payload)
        if worker.get("pending_req_id") == req_id:
            worker["pending_req_id"] = None


def poll_policy_worker(worker: dict[str, Any]) -> None:
    while True:
        try:
            kind, req_id, payload = worker["responses"].get_nowait()
        except queue.Empty:
            return
        handle_worker_message(worker, kind, req_id, payload)


def wait_policy_worker_until(worker: dict[str, Any], deadline: float) -> None:
    while not worker.get("ready") and not worker.get("error"):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        try:
            kind, req_id, payload = worker["responses"].get(timeout=remaining)
        except queue.Empty:
            return
        handle_worker_message(worker, kind, req_id, payload)


def request_policy_action(worker: dict[str, Any], state, side: Side, deadline: float) -> CommandAction | None:
    if not worker.get("ready") or worker.get("error") or worker.get("pending_req_id") is not None:
        return None
    worker["next_req_id"] += 1
    req_id = worker["next_req_id"]
    try:
        worker["requests"].put_nowait((req_id, state.clone(), side))
        worker["pending_req_id"] = req_id
    except queue.Full:
        return None

    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return None
        try:
            kind, got_req_id, payload = worker["responses"].get(timeout=remaining)
        except queue.Empty:
            return None
        if kind == "action" and got_req_id == req_id:
            worker["pending_req_id"] = None
            return payload
        handle_worker_message(worker, kind, got_req_id, payload)


def stop_policy_worker(worker: dict[str, Any] | None) -> None:
    if worker is None:
        return
    try:
        worker["requests"].put_nowait(None)
    except Exception:
        pass
    proc = worker.get("proc")
    if proc is not None:
        proc.join(timeout=0.2)
        if proc.is_alive():
            proc.terminate()


def compute_own_charges_and_targets(state, player: Side, action: CommandAction) -> None:
    pstate = state.player(player)
    for region in action.upgrades:
        b = state.buildings.get(region)
        if b is None:
            pstate.gold -= BASE_BUILD_COST
        else:
            cost = b.repair_cost if b.level >= b.max_level else b.next_level_cost
            pstate.gold -= int(cost or 0)
    # The judge charges moves after construction, so a region we UPGRADE this
    # turn holds our building by move-charge time and the move is free. The
    # building itself is created later by the result block's UPGRADE event.
    upgraded_this_turn = set(action.upgrades)
    for move in action.moves:
        if move.target not in upgraded_this_turn:
            pstate.gold -= move_command_cost(state, player, move.target)
        unit = state.units.get(move.unit_id)
        if unit is not None:
            unit.target = move.target
    pstate.gold -= TRAIN_COST * action.train_n


def apply_upgrade_event(state, side: Side, region: int) -> None:
    b = state.buildings.get(region)
    if b is None:
        state.buildings[region] = Building(side, BuildingKind.BASE, 1, 6)
    elif b.owner is side:
        b.upgrade_or_repair()


def apply_train_event(state, uid: UnitId) -> None:
    hq = state.get_hq(uid.owner)
    if hq is None:
        return
    pstate = state.player(uid.owner)
    pstate.next_unit_id = max(pstate.next_unit_id, uid.number + 1)
    hp = HQ_TABLE[hq.level].warrior_hp
    state.units[uid] = Unit(uid, uid.owner, pstate.hq_pos, hp, hp)


def apply_evening_gold_estimate(state) -> None:
    for side in (Side.LEFT, Side.RIGHT):
        consumed = set()
        for region, building in state.buildings_for(side):
            units = [u for u in state.get_units_at(region, side) if u.id not in consumed]
            for unit in units[: building.worker_cap]:
                consumed.add(unit.id)
                state.player(side).gold += INCOME_PER_WORKER
        for _unit in state.units_for(side):
            if state.player(side).gold >= FOOD_COST:
                state.player(side).gold -= FOOD_COST


def read_turn_result(state, player: Side, enemy_moving: set[UnitId]) -> bool:
    line = readln()
    if line == "FINISH":
        return False
    if not line.startswith("TURN"):
        raise SystemExit(f"expected TURN or FINISH, got {line}")
    time_line = read_tokens()
    if len(time_line) == 5 and time_line[0] == "TIME":
        state.player(player).remaining_time_tokens = int(time_line[2])
        state.player(player.opponent).remaining_time_tokens = int(time_line[4])

    opponent = player.opponent

    section = read_tokens()
    if section[0] != "UPGRADE":
        raise SystemExit("expected UPGRADE")
    for _ in range(int(section[1])):
        side_tok, region_tok = read_tokens()
        side = Side.from_letter(side_tok)
        region = int(region_tok)
        if side is opponent:
            # Enemy spend is exactly inferable from the tracked pre-event
            # building state (own charges were deducted at submit time).
            b = state.buildings.get(region)
            if b is None:
                cost = BASE_BUILD_COST
            else:
                cost = b.repair_cost if b.level >= b.max_level else b.next_level_cost
            state.player(opponent).gold -= int(cost or 0)
        apply_upgrade_event(state, side, region)

    section = read_tokens()
    if section[0] != "TRAIN":
        raise SystemExit("expected TRAIN")
    n = int(section[1])
    if n > 0:
        for tok in read_tokens():
            uid = UnitId.parse(tok)
            if uid.owner is opponent:
                state.player(opponent).gold -= TRAIN_COST
            apply_train_event(state, uid)

    section = read_tokens()
    if section[0] != "MOVE":
        raise SystemExit("expected MOVE")
    enemy_moved_now: set[UnitId] = set()
    for _ in range(int(section[1])):
        uid_tok, region_tok = read_tokens()
        uid = UnitId.parse(uid_tok)
        region = int(region_tok)
        unit = state.units.get(uid)
        if unit is not None:
            unit.pos = region
            if uid.owner is player and unit.target == region:
                unit.target = None
            elif uid.owner is not player:
                unit.target = None
        if uid.owner is opponent:
            enemy_moved_now.add(uid)
            if uid not in enemy_moving:
                # First observed step of a stationary enemy unit implies a new
                # MOVE command this turn. Slightly over-deducts when the hidden
                # destination is an enemy-owned building (free move) and misses
                # blocked-unit resumes; acceptable per FIX_SPEC section 3.
                state.player(opponent).gold -= MOVE_COST
    # A flagged unit that stopped appearing in MOVE for one turn is treated as
    # arrived (or blocked); its next observed step counts as a new command.
    enemy_moving.clear()
    enemy_moving.update(enemy_moved_now)

    section = read_tokens()
    if section[0] != "DAMAGE":
        raise SystemExit("expected DAMAGE")
    for _ in range(int(section[1])):
        _cause, uid_tok, damage_tok = read_tokens()
        uid = UnitId.parse(uid_tok)
        unit = state.units.get(uid)
        if unit is not None:
            unit.hp -= int(damage_tok)
    for uid in [uid for uid, unit in state.units.items() if unit.hp <= 0]:
        del state.units[uid]

    section = read_tokens()
    if section[0] != "SIEGE":
        raise SystemExit("expected SIEGE")
    for _ in range(int(section[1])):
        _side_tok, region_tok, damage_tok = read_tokens()
        region = int(region_tok)
        building = state.buildings.get(region)
        if building is not None:
            building.hp -= int(damage_tok)
    for region in [region for region, building in state.buildings.items() if building.hp <= 0]:
        del state.buildings[region]

    end = readln()
    if end != "END":
        raise SystemExit(f"expected END, got {end}")
    apply_evening_gold_estimate(state)
    if state.player(opponent).gold < 0:
        state.player(opponent).gold = 0
    return True


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--device", default="cpu", help="Ignored; submit bot always runs policy inference on CPU.")
    args = parser.parse_args()
    side, state = parse_ready()
    worker = start_policy_worker(args.checkpoint, args.device, state, side)
    wait_policy_worker_until(worker, time.monotonic() + 0.75)
    print("OK", flush=True)

    enemy_moving: set[UnitId] = set()
    try:
        while True:
            line = readln()
            if line == "FINISH":
                return
            if not line.startswith("START"):
                raise SystemExit(f"expected START or FINISH, got {line}")
            deadline = time.monotonic() + 0.085
            poll_policy_worker(worker)
            wait_policy_worker_until(worker, deadline)
            action = request_policy_action(worker, state, side, deadline)
            if action is None or not validate_action(state, side, action):
                action = CommandAction()
            compute_own_charges_and_targets(state, side, action)
            sys.stdout.write("\n".join(action_to_commands(action)) + "\n")
            sys.stdout.flush()
            if not read_turn_result(state, side, enemy_moving):
                return
    finally:
        stop_policy_worker(worker)


if __name__ == "__main__":
    main()

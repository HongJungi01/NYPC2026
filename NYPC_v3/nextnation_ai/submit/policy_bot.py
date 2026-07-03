#!/usr/bin/env python3
"""Interactive bot backed by a SimpleNeuralPolicy checkpoint."""

from __future__ import annotations

import argparse
import os
import sys

from nextnation_ai.game.actions import CommandAction, MoveCommand, action_to_commands
from nextnation_ai.game.constants import BASE_BUILD_COST, FOOD_COST, INCOME_PER_WORKER, MOVE_COST, TRAIN_COST
from nextnation_ai.game.legal import move_command_cost, validate_action
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.state import Building, BuildingKind, Side, Unit, UnitId, create_initial_state
from nextnation_ai.nn.simple_policy import SimpleNeuralPolicy


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
    # turn holds our building by move-charge time and the move is free.
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
    from nextnation_ai.game.constants import HQ_TABLE
    hp = HQ_TABLE[hq.level].warrior_hp
    state.units[uid] = Unit(uid, uid.owner, pstate.hq_pos, hp, hp)


def apply_evening_gold_estimate(state) -> None:
    for side in (Side.LEFT, Side.RIGHT):
        income = 0
        consumed = set()
        for region, building in state.buildings_for(side):
            units = [u for u in state.get_units_at(region, side) if u.id not in consumed]
            for unit in units[: building.worker_cap]:
                consumed.add(unit.id)
                income += INCOME_PER_WORKER
        state.player(side).gold += income
        for unit in state.units_for(side):
            if state.player(side).gold >= FOOD_COST:
                state.player(side).gold -= FOOD_COST


def read_turn_result_full(state, player: Side, submitted: CommandAction, enemy_moving: set[UnitId]) -> bool:
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
                state.player(opponent).gold -= MOVE_COST
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
    parser.add_argument("--checkpoint", default=os.environ.get("NN_POLICY_CHECKPOINT"))
    args = parser.parse_args()
    if not args.checkpoint:
        raise SystemExit("--checkpoint or NN_POLICY_CHECKPOINT is required")
    policy = SimpleNeuralPolicy.load(args.checkpoint)
    side, state = parse_ready()
    print("OK", flush=True)

    enemy_moving: set[UnitId] = set()
    while True:
        line = readln()
        if line == "FINISH":
            return
        if not line.startswith("START"):
            raise SystemExit(f"expected START or FINISH, got {line}")
        decision = policy.select_action(state, side, deterministic=True)
        action = decision.action if validate_action(state, side, decision.action) else CommandAction()
        compute_own_charges_and_targets(state, side, action)
        sys.stdout.write("\n".join(action_to_commands(action)) + "\n")
        sys.stdout.flush()
        if not read_turn_result_full(state, side, action, enemy_moving):
            return


if __name__ == "__main__":
    main()

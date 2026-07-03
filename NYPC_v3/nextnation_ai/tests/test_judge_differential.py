"""Differential test: local Simulator vs the official judge (testing-tool.py).

The judge is the ground truth. Identical commands are applied to both engines
and the full state (gold, every unit's pos/hp/target, every building, winner)
is compared after every day. Any change to game/simulator.py, game/map.py,
game/state.py, or game/legal.py must keep this green.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path

from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.simulator import Simulator
from nextnation_ai.game.state import Side, create_initial_state

_JUDGE_PATH = Path(__file__).resolve().parents[2] / "nation-providing" / "testing-tool.py"


def _load_judge():
    spec = importlib.util.spec_from_file_location("nextnation_judge", _JUDGE_PATH)
    assert spec is not None and spec.loader is not None, f"cannot import {_JUDGE_PATH}"
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _action_to_lines(action: CommandAction) -> list[str]:
    lines = [f"UPGRADE {r}" for r in action.upgrades]
    lines += [f"MOVE {mv.unit_id} {mv.target}" for mv in action.moves]
    lines.append(f"TRAIN {action.train_n}")
    return lines


def _judge_snapshot(judge, jst):
    warriors = {
        judge.id_str(k): (w.region, w.hp, w.moving_target)
        for k, w in jst.warriors.items()
        if w.hp > 0
    }
    buildings = {r: (b.side.value, b.kind.value, b.level, b.hp) for r, b in jst.buildings.items()}
    return tuple(jst.gold), tuple(sorted(warriors.items())), tuple(sorted(buildings.items()))


def _sim_snapshot(sst):
    warriors = {str(u.id): (u.pos, u.hp, u.target) for u in sst.units.values() if u.hp > 0}
    buildings = {r: (b.owner.value, b.kind.value, b.level, b.hp) for r, b in sst.buildings.items()}
    gold = (sst.players[Side.LEFT].gold, sst.players[Side.RIGHT].gold)
    return gold, tuple(sorted(warriors.items())), tuple(sorted(buildings.items()))


def _run_judge_day(judge, jst, m, day, lines_left, lines_right):
    jst.day = day
    rb_l, rb_r = judge.ResultBlock(), judge.ResultBlock()
    sub_l = judge.parse_block(judge.Side.LEFT, lines_left)
    judge.apply_upgrades(jst, m, judge.Side.LEFT, sub_l, rb_l)
    judge.apply_moves(jst, m, judge.Side.LEFT, sub_l)
    train_l = judge.apply_train_charge(jst, judge.Side.LEFT, sub_l)
    sub_r = judge.parse_block(judge.Side.RIGHT, lines_right)
    judge.apply_upgrades(jst, m, judge.Side.RIGHT, sub_r, rb_r)
    judge.apply_moves(jst, m, judge.Side.RIGHT, sub_r)
    train_r = judge.apply_train_charge(jst, judge.Side.RIGHT, sub_r)

    siege: dict[int, int] = {}
    judge.apply_day_movement(jst, m, rb_l, rb_r)
    judge.spawn_trained(jst, judge.Side.LEFT, train_l, rb_l)
    judge.spawn_trained(jst, judge.Side.RIGHT, train_r, rb_r)
    judge.apply_day_combat(jst, rb_l, rb_r, siege)
    judge.apply_day_siege(jst, rb_l, rb_r, siege)
    judge.apply_evening_work(jst)
    judge.apply_evening_upkeep(jst, rb_l, rb_r)


def _make_game(judge, seed: int):
    judge._dijkstra_cache.clear()  # keyed by target only; stale across maps
    rows = judge.generate_map(judge.XoShiro256(seed), NP=25, KP=4)
    m = judge.read_map(rows)
    game_map = GameMap.from_lines(rows)
    return m, judge.init_state(m), create_initial_state(game_map)


def test_random_games_match_judge_state_daily():
    judge = _load_judge()
    sim = Simulator()
    for seed in (1000, 1001):
        m, jst, sst = _make_game(judge, seed)
        left = RandomLegalAgent(seed=seed * 2 + 1, max_upgrades_per_turn=2,
                                max_move_origins_per_turn=4,
                                upgrade_probability=0.4, move_probability=0.5)
        right = RandomLegalAgent(seed=seed * 2 + 2, max_upgrades_per_turn=2,
                                 max_move_origins_per_turn=4,
                                 upgrade_probability=0.4, move_probability=0.5)
        for day in range(1, 61):
            a_l = left.act(sst, Side.LEFT)
            a_r = right.act(sst, Side.RIGHT)
            _run_judge_day(judge, jst, m, day, _action_to_lines(a_l), _action_to_lines(a_r))
            result = sim.step(sst, a_l, a_r)
            assert not result.invalid_sides, f"seed {seed} day {day}: sim flagged {result.invalid_sides}"
            sst = result.state
            assert _judge_snapshot(judge, jst) == _sim_snapshot(sst), f"seed {seed} day {day}: state diverged"
            if judge.any_hq_destroyed(jst) or sst.done:
                assert judge.any_hq_destroyed(jst) == (sst.done and sst.terminal_reason == "HQ_DESTROYED")
                break


def _budgeted_rush(state, side: Side) -> CommandAction:
    target = state.players[side.opponent].hq_pos
    budget = state.players[side].gold
    train = 1 if budget >= 120 else 0
    budget -= 120 * train
    moves = []
    for unit in state.units_for(side):
        if unit.target is None and budget >= 10:
            moves.append(MoveCommand(unit.id, target))
            budget -= 10
    return CommandAction(train_n=train, moves=moves)


def test_one_sided_rush_hq_destruction_matches_judge():
    judge = _load_judge()
    sim = Simulator()
    m, jst, sst = _make_game(judge, 777)
    destroyed = False
    for day in range(1, 201):
        a_l = _budgeted_rush(sst, Side.LEFT)
        a_r = CommandAction()
        _run_judge_day(judge, jst, m, day, _action_to_lines(a_l), _action_to_lines(a_r))
        sst = sim.step(sst, a_l, a_r).state
        assert _judge_snapshot(judge, jst) == _sim_snapshot(sst), f"day {day}: state diverged"
        if judge.any_hq_destroyed(jst):
            destroyed = True
            assert sst.done and sst.terminal_reason == "HQ_DESTROYED"
            left_hq = judge.hq_of(jst, judge.Side.LEFT)
            right_hq = judge.hq_of(jst, judge.Side.RIGHT)
            left_score = (left_hq is not None, left_hq.hp if left_hq else 0)
            right_score = (right_hq is not None, right_hq.hp if right_hq else 0)
            judge_winner = "LEFT" if left_score > right_score else ("RIGHT" if left_score < right_score else "DRAW")
            sim_winner = sst.winner.word if sst.winner is not None else "DRAW"
            assert judge_winner == sim_winner
            break
    assert destroyed, "rush scenario failed to destroy an HQ; terminal path untested"

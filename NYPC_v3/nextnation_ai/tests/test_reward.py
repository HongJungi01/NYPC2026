"""Reward invariants: antisymmetry, zero-sum shaping, and no idle penalty."""

from nextnation_ai.game.actions import CommandAction
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.reward import (
    DRAW_REWARD,
    GAMMA,
    potential_score,
    shaped_reward,
    terminal_reward,
)
from nextnation_ai.game.simulator import Simulator
from nextnation_ai.game.state import Building, BuildingKind, Side, UnitId, create_initial_state


def _asymmetric_state():
    game_map = GameMap(5, 1, [-2, -1, 0, 1, 2], [0, 0, 0, 0, 0], [2], [[1], [0, 2], [1, 3], [2, 4], [3]])
    state = create_initial_state(game_map)
    state.units[UnitId(Side.LEFT, 1)].pos = 2
    state.units[UnitId(Side.LEFT, 2)].hp = 1
    state.buildings[2] = Building(Side.LEFT, BuildingKind.BASE, 2, 7)
    state.buildings[4].hp = 4
    return state


def test_potential_is_antisymmetric():
    for state in (create_initial_state(GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])), _asymmetric_state()):
        left = potential_score(state, Side.LEFT)
        right = potential_score(state, Side.RIGHT)
        assert abs(left + right) < 1e-12


def test_shaping_is_zero_sum_before_terminal():
    state = _asymmetric_state()
    result = Simulator().step(state, CommandAction(), CommandAction())
    assert not result.state.done
    r_left = shaped_reward(state, result.state, Side.LEFT, beta=0.2)
    r_right = shaped_reward(state, result.state, Side.RIGHT, beta=0.2)
    assert abs(r_left + r_right) < 1e-12


def test_quiet_day_is_not_penalized():
    # Regression for the removed activity shaping: saving gold (an empty but
    # legal command list) must cost at most the tiny (1 - GAMMA) * Phi leak.
    game_map = GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])
    state = create_initial_state(game_map)
    result = Simulator().step(state, CommandAction(), CommandAction())
    reward = shaped_reward(state, result.state, Side.LEFT, beta=0.2)
    assert abs(reward) < 0.01


def test_terminal_reward_values():
    game_map = GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])
    state = create_initial_state(game_map)
    assert terminal_reward(state, Side.LEFT) == 0.0

    state.done = True
    state.winner = Side.LEFT
    assert terminal_reward(state, Side.LEFT) == 1.0
    assert terminal_reward(state, Side.RIGHT) == -1.0

    # A draw is slightly punished for BOTH sides so that mutual passivity is
    # not a comfortable self-play equilibrium (BC_SPEC.md section 3).
    state.winner = None
    assert terminal_reward(state, Side.LEFT) == DRAW_REWARD
    assert terminal_reward(state, Side.RIGHT) == DRAW_REWARD
    assert -1.0 < DRAW_REWARD < 0.0


def test_shaped_reward_matches_definition():
    state = _asymmetric_state()
    result = Simulator().step(state, CommandAction(), CommandAction())
    expected = terminal_reward(result.state, Side.LEFT) + 0.2 * (
        GAMMA * potential_score(result.state, Side.LEFT) - potential_score(state, Side.LEFT)
    )
    assert abs(shaped_reward(state, result.state, Side.LEFT, beta=0.2) - expected) < 1e-12

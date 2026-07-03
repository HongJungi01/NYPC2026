"""Action decoding regressions."""

from nextnation_ai.game.actions import MoveGroupAction, NNAction, decode_nn_action
from nextnation_ai.game.legal import validate_action
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.state import Side, create_initial_state


def test_decode_same_origin_groups_never_duplicate_units():
    game_map = GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])
    state = create_initial_state(game_map)  # 3 idle warriors at node 0

    action = NNAction(moves=[
        MoveGroupAction(origin=0, target=1, count=2),
        MoveGroupAction(origin=0, target=1, count=2),
    ])
    command = decode_nn_action(state, Side.LEFT, action)

    unit_ids = [move.unit_id for move in command.moves]
    assert len(unit_ids) == 3  # 2 + capped-at-remaining 1
    assert len(set(unit_ids)) == 3
    assert validate_action(state, Side.LEFT, command)


def test_decode_noop_group_produces_no_moves():
    game_map = GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])
    state = create_initial_state(game_map)
    action = NNAction(moves=[MoveGroupAction(origin=0, target=None, count=3)])
    assert decode_nn_action(state, Side.LEFT, action).moves == []

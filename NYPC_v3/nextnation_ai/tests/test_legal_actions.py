from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.legal import legal_train_mask, validate_action
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.state import Side, UnitId, create_initial_state


def test_train_mask_uses_hq_cap_and_gold():
    state = create_initial_state(GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]]))
    assert legal_train_mask(state, Side.LEFT) == [True, True, False, False]
    state.player(Side.LEFT).gold = 119
    assert legal_train_mask(state, Side.LEFT) == [True, False, False, False]


def test_build_requires_friendly_and_no_enemy_unit():
    state = create_initial_state(GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]]))
    assert not validate_action(state, Side.LEFT, CommandAction(upgrades=[1]))
    state.units[UnitId(Side.LEFT, 1)].pos = 1
    assert validate_action(state, Side.LEFT, CommandAction(upgrades=[1]))
    state.units[UnitId(Side.RIGHT, 1)].pos = 1
    assert not validate_action(state, Side.LEFT, CommandAction(upgrades=[1]))


def test_duplicate_move_is_invalid():
    state = create_initial_state(GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]]))
    uid = UnitId(Side.LEFT, 1)
    action = CommandAction(moves=[MoveCommand(uid, 1), MoveCommand(uid, 2)])
    assert not validate_action(state, Side.LEFT, action)

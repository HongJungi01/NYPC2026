from nextnation_ai.game.actions import CommandAction
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.simulator import Simulator
from nextnation_ai.game.state import Side, UnitId, create_initial_state


def tiny_map():
    return GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])


def test_zero_hp_warrior_still_contributes_precomputed_attack_count():
    state = create_initial_state(tiny_map())
    state.units[UnitId(Side.LEFT, 1)].pos = 1
    state.units[UnitId(Side.LEFT, 1)].hp = 1
    state.units[UnitId(Side.RIGHT, 1)].pos = 1
    state.units[UnitId(Side.RIGHT, 1)].hp = 1
    for uid in [UnitId(Side.LEFT, 2), UnitId(Side.LEFT, 3), UnitId(Side.RIGHT, 2), UnitId(Side.RIGHT, 3)]:
        del state.units[uid]

    result = Simulator().step(state, CommandAction(), CommandAction())
    damaged = {(cause, str(uid)): dmg for cause, uid, dmg in result.events.damages}

    assert damaged[("COMBAT", "A1")] == 1
    assert damaged[("COMBAT", "B1")] == 1
    assert UnitId(Side.LEFT, 1) not in result.state.units
    assert UnitId(Side.RIGHT, 1) not in result.state.units

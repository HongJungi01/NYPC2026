from nextnation_ai.game.actions import CommandAction
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.simulator import Simulator
from nextnation_ai.game.state import Side, create_initial_state


def test_labor_then_supply_updates_gold():
    game_map = GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])
    state = create_initial_state(game_map)

    result = Simulator().step(state, CommandAction(), CommandAction())

    # Each level-1 HQ earns 15 from one worker, then pays 2 for each of 3 units.
    assert result.state.player(Side.LEFT).gold == 509
    assert result.state.player(Side.RIGHT).gold == 509


def test_supply_hunger_damages_units_in_id_order():
    game_map = GameMap(3, 1, [-1, 0, 1], [0, 0, 0], [1], [[1], [0, 2], [1]])
    state = create_initial_state(game_map)
    state.player(Side.LEFT).gold = 0
    del state.buildings[0]

    result = Simulator().step(state, CommandAction(), CommandAction())
    hunger = [(str(uid), dmg) for cause, uid, dmg in result.events.damages if cause == "HUNGER" and uid.owner is Side.LEFT]

    assert hunger == [("A1", 1), ("A2", 1), ("A3", 1)]

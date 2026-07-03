from nextnation_ai.game.map import GameMap


def test_next_hop_tie_breaks_to_smaller_neighbor():
    game_map = GameMap(
        4,
        1,
        [0, 1, 1, 2],
        [0, 0, 0, 0],
        [1],
        [[1, 2], [0, 3], [0, 3], [1, 2]],
    )

    assert game_map.next_step(0, 0) is None
    assert game_map.next_step(0, 3) == 1
    assert game_map.next_step(0, 3) in game_map.adj[0]


def test_unreachable_has_no_next_hop():
    game_map = GameMap(3, 1, [0, 1, 2], [0, 0, 0], [1], [[1], [0], []])
    assert not game_map.is_reachable(0, 2)
    assert game_map.next_step(0, 2) is None

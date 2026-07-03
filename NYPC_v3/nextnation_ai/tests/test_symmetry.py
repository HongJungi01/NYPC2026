from nextnation_ai.game.map import GameMap, canonical_node


def test_right_canonical_node_is_its_own_inverse():
    n = 7
    for node in range(n):
        assert canonical_node(canonical_node(node, 1, n), 1, n) == node


def test_canonicalizing_right_twice_restores_line_map_geometry():
    game_map = GameMap(
        5,
        1,
        [-2, -1, 0, 1, 2],
        [0, 0, 0, 0, 0],
        [2],
        [[1], [0, 2], [1, 3], [2, 4], [3]],
    )

    right = game_map.canonicalize(1)
    restored = right.canonicalize(1)

    assert restored.x == game_map.x
    assert restored.y == game_map.y
    assert restored.points == game_map.points
    assert restored.adj == game_map.adj

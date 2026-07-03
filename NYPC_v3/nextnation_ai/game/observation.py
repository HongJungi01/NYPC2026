"""Canonical graph observation builder."""

from __future__ import annotations

import random as _random
from dataclasses import dataclass

from .constants import MAX_DAYS
from .legal import upgrade_cost
from .map import INF_DISTANCE, absolute_node, canonical_node
from .state import BuildingKind, GameState, Side, Unit


NODE_FEATURE_NAMES = [
    "x_norm",
    "y_norm",
    "is_point",
    "is_my_hq",
    "is_enemy_hq",
    "degree_norm",
    "dist_to_my_hq_norm",
    "dist_to_enemy_hq_norm",
    "dist_to_center_norm",
    "my_building_exists",
    "my_building_is_hq",
    "my_building_is_base",
    "my_building_level_norm",
    "my_building_hp_ratio",
    "my_turret_power_norm",
    "my_worker_cap_norm",
    "enemy_building_exists",
    "enemy_building_is_hq",
    "enemy_building_is_base",
    "enemy_building_level_norm",
    "enemy_building_hp_ratio",
    "enemy_turret_power_norm",
    "enemy_worker_cap_norm",
    "my_unit_count_norm",
    "my_idle_count_norm",
    "my_moving_count_norm",
    "my_hp_sum_norm",
    "my_hp_mean_norm",
    "my_min_hp_norm",
    "enemy_unit_count_norm",
    "enemy_hp_sum_norm",
    "enemy_hp_mean_norm",
    "enemy_min_hp_norm",
    "my_income_here_norm",
    "enemy_income_here_norm",
    "local_hp_balance_norm",
    "adjacent_my_units_norm",
    "adjacent_enemy_units_norm",
    "can_build_or_upgrade",
]

EDGE_FEATURE_NAMES = [
    "edge_length_norm",
    "dx_norm",
    "dy_norm",
    "is_forward_edge",
    "is_backward_edge",
]

GLOBAL_FEATURE_NAMES = [
    "day_norm",
    "my_gold_norm",
    "enemy_gold_estimate_norm",
    "my_hq_level_norm",
    "enemy_hq_level_norm",
    "my_hq_hp_ratio",
    "enemy_hq_hp_ratio",
    "my_total_units_norm",
    "enemy_total_units_norm",
    "my_total_buildings_norm",
    "enemy_total_buildings_norm",
    "remaining_days_norm",
]


# Training-time noise for the enemy-gold feature, as (lo_mult, hi_mult, sigma).
# None means the exact value is used (inference feeds an estimate instead, see
# submit/torch_policy_bot.py). The draw is seeded from the state so that PPO
# re-evaluation of a stored state reproduces the identical observation.
ENEMY_GOLD_NOISE: tuple[float, float, float] | None = None


def _enemy_gold_feature(state: GameState, player: Side) -> float:
    gold = state.player(player.opponent).gold
    if ENEMY_GOLD_NOISE is None:
        return gold / 5000.0
    lo, hi, sigma = ENEMY_GOLD_NOISE
    # String seeds hash deterministically (sha512) across processes, so PPO
    # re-evaluation of a stored state reproduces the identical observation.
    rng = _random.Random(f"{state.day},{state.player(player).gold},{gold},{int(player)}")
    noised = gold * rng.uniform(lo, hi) + rng.gauss(0.0, sigma)
    return max(0.0, noised) / 5000.0


@dataclass
class Observation:
    node_features: list[list[float]]
    edge_index: list[tuple[int, int]]
    edge_features: list[list[float]]
    global_features: list[float]
    player: Side
    node_feature_names: list[str]
    edge_feature_names: list[str]
    global_feature_names: list[str]


def _canonical_xy(state: GameState, absolute: int, player: Side) -> tuple[float, float]:
    x = state.map.x[absolute]
    y = state.map.y[absolute]
    if player is Side.RIGHT:
        x = -x
        y = -y
    return float(x), float(y)


def _unit_stats(units: list[Unit]) -> tuple[float, float, float, float]:
    if not units:
        return 0.0, 0.0, 0.0, 0.0
    hp_sum = sum(u.hp for u in units)
    return (
        len(units) / 20.0,
        hp_sum / 100.0,
        (hp_sum / len(units)) / 10.0,
        min(u.hp for u in units) / 10.0,
    )


def _building_features(state: GameState, absolute: int, owner: Side) -> list[float]:
    building = state.buildings.get(absolute)
    if building is None or building.owner is not owner:
        return [0.0] * 7
    return [
        1.0,
        1.0 if building.kind is BuildingKind.HQ else 0.0,
        1.0 if building.kind is BuildingKind.BASE else 0.0,
        building.level / 5.0,
        building.hp / max(1, building.max_hp),
        building.turret / 3.0,
        building.worker_cap / 5.0,
    ]


def _income_here(state: GameState, absolute: int, owner: Side, units_by_pos: dict[int, list[Unit]]) -> float:
    building = state.buildings.get(absolute)
    if building is None or building.owner is not owner:
        return 0.0
    return min(len(units_by_pos.get(absolute, ())), building.worker_cap) * 15.0 / 100.0


def _alive_units_by_pos(state: GameState, owner: Side) -> dict[int, list[Unit]]:
    by_pos: dict[int, list[Unit]] = {}
    for unit in state.units.values():
        if unit.owner is owner and unit.alive:
            by_pos.setdefault(unit.pos, []).append(unit)
    return by_pos


def _can_upgrade_here(
    state: GameState,
    player: Side,
    region: int,
    gold: int,
    my_units: list[Unit],
    enemy_units: list[Unit],
) -> bool:
    # Same result as legal.can_upgrade_at, but using the precomputed unit
    # indexes instead of rescanning state.units per region.
    if enemy_units or not my_units:
        return False
    cost = upgrade_cost(state, player, region)
    return cost is not None and cost <= gold


def build_node_features(state: GameState, player: Side) -> list[list[float]]:
    n = state.map.N
    max_coord = max(1.0, *(abs(v) for v in state.map.x), *(abs(v) for v in state.map.y))
    finite_dist = [
        d
        for row in state.map.dist
        for d in row
        if d < INF_DISTANCE
    ]
    max_dist = max(1.0, float(max(finite_dist) if finite_dist else 1.0))
    my_hq = state.player(player).hq_pos
    enemy = player.opponent
    enemy_hq = state.player(enemy).hq_pos
    center = n // 2
    my_by_pos = _alive_units_by_pos(state, player)
    enemy_by_pos = _alive_units_by_pos(state, enemy)
    my_gold = state.player(player).gold
    features: list[list[float]] = []

    for c in range(n):
        absolute = absolute_node(c, player, n)
        x, y = _canonical_xy(state, absolute, player)
        my_units = my_by_pos.get(absolute, [])
        enemy_units = enemy_by_pos.get(absolute, [])
        my_unit_count, my_hp_sum, my_hp_mean, my_min_hp = _unit_stats(my_units)
        enemy_unit_count, enemy_hp_sum, enemy_hp_mean, enemy_min_hp = _unit_stats(enemy_units)
        my_idle = len([u for u in my_units if u.target is None]) / 20.0
        my_moving = len([u for u in my_units if u.target is not None]) / 20.0
        adjacent_my = sum(len(my_by_pos.get(nb, ())) for nb in state.map.adj[absolute]) / 20.0
        adjacent_enemy = sum(len(enemy_by_pos.get(nb, ())) for nb in state.map.adj[absolute]) / 20.0
        row = [
            x / max_coord,
            y / max_coord,
            1.0 if absolute in state.map.points else 0.0,
            1.0 if absolute == my_hq else 0.0,
            1.0 if absolute == enemy_hq else 0.0,
            len(state.map.adj[absolute]) / max(1, n - 1),
            state.map.dist[absolute][my_hq] / max_dist,
            state.map.dist[absolute][enemy_hq] / max_dist,
            state.map.dist[absolute][center] / max_dist,
            *_building_features(state, absolute, player),
            *_building_features(state, absolute, enemy),
            my_unit_count,
            my_idle,
            my_moving,
            my_hp_sum,
            my_hp_mean,
            my_min_hp,
            enemy_unit_count,
            enemy_hp_sum,
            enemy_hp_mean,
            enemy_min_hp,
            _income_here(state, absolute, player, my_by_pos),
            _income_here(state, absolute, enemy, enemy_by_pos),
            my_hp_sum - enemy_hp_sum,
            adjacent_my,
            adjacent_enemy,
            1.0 if _can_upgrade_here(state, player, absolute, my_gold, my_units, enemy_units) else 0.0,
        ]
        features.append(row)
    return features


def build_edge_features(state: GameState, player: Side) -> tuple[list[tuple[int, int]], list[list[float]]]:
    n = state.map.N
    max_coord = max(1.0, *(abs(v) for v in state.map.x), *(abs(v) for v in state.map.y))
    max_edge = max(
        1.0,
        *(
            state.map.edge_length(u, v)
            for u in range(n)
            for v in state.map.adj[u]
        ),
    )
    edge_index: list[tuple[int, int]] = []
    edge_features: list[list[float]] = []
    for absolute_u in range(n):
        cu = canonical_node(absolute_u, player, n)
        ux, uy = _canonical_xy(state, absolute_u, player)
        for absolute_v in state.map.adj[absolute_u]:
            cv = canonical_node(absolute_v, player, n)
            vx, vy = _canonical_xy(state, absolute_v, player)
            dx = vx - ux
            dy = vy - uy
            edge_index.append((cu, cv))
            edge_features.append([
                state.map.edge_length(absolute_u, absolute_v) / max_edge,
                dx / max_coord,
                dy / max_coord,
                1.0 if dx > 0 else 0.0,
                1.0 if dx < 0 else 0.0,
            ])
    return edge_index, edge_features


def build_global_features(state: GameState, player: Side) -> list[float]:
    enemy = player.opponent
    my_hq = state.get_hq(player)
    enemy_hq = state.get_hq(enemy)
    return [
        state.day / MAX_DAYS,
        state.player(player).gold / 5000.0,
        _enemy_gold_feature(state, player),
        (my_hq.level if my_hq else 0) / 5.0,
        (enemy_hq.level if enemy_hq else 0) / 5.0,
        (my_hq.hp / max(1, my_hq.max_hp)) if my_hq else 0.0,
        (enemy_hq.hp / max(1, enemy_hq.max_hp)) if enemy_hq else 0.0,
        len(state.units_for(player)) / 100.0,
        len(state.units_for(enemy)) / 100.0,
        len(state.buildings_for(player)) / 20.0,
        len(state.buildings_for(enemy)) / 20.0,
        max(0, MAX_DAYS - state.day) / MAX_DAYS,
    ]


def build_observation(state: GameState, player: Side) -> Observation:
    edge_index, edge_features = build_edge_features(state, player)
    return Observation(
        build_node_features(state, player),
        edge_index,
        edge_features,
        build_global_features(state, player),
        player,
        list(NODE_FEATURE_NAMES),
        list(EDGE_FEATURE_NAMES),
        list(GLOBAL_FEATURE_NAMES),
    )


def make_pyg_data(obs: Observation):
    try:
        import torch
        from torch_geometric.data import Data
    except ImportError as exc:
        raise ImportError("make_pyg_data requires torch and torch_geometric") from exc

    edge_index = torch.tensor(obs.edge_index, dtype=torch.long).t().contiguous()
    data = Data(
        x=torch.tensor(obs.node_features, dtype=torch.float32),
        edge_index=edge_index,
        edge_attr=torch.tensor(obs.edge_features, dtype=torch.float32),
    )
    data.global_feat = torch.tensor(obs.global_features, dtype=torch.float32)
    return data

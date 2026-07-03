"""Potential-based reward helpers.

The shaped reward is strictly ``terminal + beta * (GAMMA * Phi(s') - Phi(s))``
(PLAN.md paragraph 11). Phi is antisymmetric between the two players, so the
shaping stays zero-sum and telescopes over an episode; annealing ``beta`` to 0
recovers pure win/loss fine-tuning. Event bonuses (activity, new-base) were
removed on purpose: they overwhelmed the terminal signal and penalized
saving gold for expensive upgrades (see FIX_SPEC.md section 1). Draws pay
``DRAW_REWARD`` to both sides and Phi weights emphasize income/buildings
(BC_SPEC.md section 3).
"""

from __future__ import annotations

from .constants import INCOME_PER_WORKER
from .state import BuildingKind, GameState, Side

# Must stay in sync with the trainer's discount (scripts/train_torch.py --gamma).
GAMMA = 0.997

# Both sides receive this on a draw. Slightly negative so that mutual
# passivity (turtle vs turtle -> exact HQ-hp tie at the turn limit) is not a
# comfortable fixed point of self-play; any win strictly dominates a draw
# (BC_SPEC.md section 3). Kept small so that under outcome-only training
# (shaping beta = 0) a draw stays close to neutral, per the win/draw/loss
# scoring idea (2026-07-03).
DRAW_REWARD = -0.1


def terminal_reward(state: GameState, player: Side) -> float:
    if not state.done:
        return 0.0
    if state.winner is None:
        return DRAW_REWARD
    return 1.0 if state.winner is player else -1.0


def _hq_hp_ratio(state: GameState, player: Side) -> float:
    hq = state.get_hq(player)
    if hq is None:
        return 0.0
    return hq.hp / max(1, hq.max_hp)


def _income_norm(state: GameState, player: Side) -> float:
    income = 0
    for region, building in state.buildings_for(player):
        workers = len(state.get_units_at(region, player))
        income += INCOME_PER_WORKER * min(workers, building.worker_cap)
    return income / 500.0


def _building_value_norm(state: GameState, player: Side) -> float:
    value = 0.0
    for _, building in state.buildings_for(player):
        level_weight = 2.0 if building.kind is BuildingKind.HQ else 1.0
        value += level_weight * building.level + building.hp / max(1, building.max_hp)
    return value / 20.0


def _unit_hp_norm(state: GameState, player: Side) -> float:
    return sum(unit.hp for unit in state.units_for(player)) / 100.0


def _map_control_norm(state: GameState, player: Side) -> float:
    if not state.map.points:
        return 0.0
    controlled = 0
    for point in state.map.points:
        my_units = len(state.get_units_at(point, player))
        enemy_units = len(state.get_units_at(point, player.opponent))
        building = state.buildings.get(point)
        building_bonus = building is not None and building.owner is player
        if my_units > enemy_units or building_bonus:
            controlled += 1
    return controlled / len(state.map.points)


def _frontline_norm(state: GameState, player: Side) -> float:
    units = state.units_for(player)
    if not units or state.map.N <= 1:
        return 0.0
    denom = state.map.N - 1
    total = 0.0
    weight = 0.0
    for unit in units:
        progress = unit.pos / denom if player is Side.LEFT else (denom - unit.pos) / denom
        hp_weight = max(1.0, unit.hp)
        total += max(0.0, min(1.0, progress)) * hp_weight
        weight += hp_weight
    return total / max(1.0, weight)


def _hq_threat_norm(state: GameState, player: Side) -> float:
    units = state.units_for(player)
    enemy_hq = state.player(player.opponent).hq_pos
    if not units or not state.map.dist:
        return 0.0
    finite = [d for row in state.map.dist for d in row if d < 10**20]
    max_dist = max(finite) if finite else 1
    best = 0.0
    for unit in units:
        d = state.map.dist[unit.pos][enemy_hq]
        if d >= 10**20:
            continue
        closeness = 1.0 - min(1.0, d / max(1, max_dist))
        best = max(best, closeness * (unit.hp / max(1, unit.max_hp)))
    return best


def potential_score(state: GameState, player: Side) -> float:
    enemy = player.opponent
    return (
        0.30 * _hq_hp_ratio(state, player)
        - 0.30 * _hq_hp_ratio(state, enemy)
        + 0.25 * _income_norm(state, player)
        - 0.25 * _income_norm(state, enemy)
        + 0.15 * _building_value_norm(state, player)
        - 0.15 * _building_value_norm(state, enemy)
        + 0.05 * _unit_hp_norm(state, player)
        - 0.05 * _unit_hp_norm(state, enemy)
        + 0.05 * _map_control_norm(state, player)
        - 0.05 * _map_control_norm(state, enemy)
        + 0.05 * _frontline_norm(state, player)
        - 0.05 * _frontline_norm(state, enemy)
        + 0.05 * _hq_threat_norm(state, player)
        - 0.05 * _hq_threat_norm(state, enemy)
    )


def shaped_reward(before: GameState, after: GameState, player: Side, beta: float = 0.20, action=None) -> float:
    return (
        terminal_reward(after, player)
        + beta * (GAMMA * potential_score(after, player) - potential_score(before, player))
    )

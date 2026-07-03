"""Dependency-free trainable macro policy.

This is not a replacement for the planned PyTorch GNN. It is a compact neural
policy with one tanh hidden layer, implemented in pure Python so the training
pipeline can run in environments where torch/numpy are unavailable. The action
space is a set of legal macro commands decoded through the rule validator.
"""

from __future__ import annotations

import copy
import json
import math
import random
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.constants import MAX_DAYS
from nextnation_ai.game.legal import legal_train_mask, legal_upgrade_mask_and_cost, validate_action
from nextnation_ai.game.state import BuildingKind, GameState, Side, Unit


ACTION_NAMES = [
    "idle",
    "train",
    "upgrade_or_build",
    "expand",
    "attack_hq",
    "defend_hq",
    "fill_worker",
    "raid_base",
]


@dataclass
class PolicyDecision:
    action: CommandAction
    action_index: int
    features: list[float]
    legal_mask: list[bool]
    value: float
    logp: float
    entropy: float


def _safe_div(a: float, b: float) -> float:
    return a / b if b else 0.0


def _income(state: GameState, player: Side) -> int:
    total = 0
    for region, building in state.buildings_for(player):
        total += 15 * min(len(state.get_units_at(region, player)), building.worker_cap)
    return total


def _avg_distance_to(state: GameState, units: list[Unit], target: int) -> float:
    if not units:
        return 1.0
    finite = [state.map.dist[u.pos][target] for u in units if state.map.is_reachable(u.pos, target)]
    if not finite:
        return 1.0
    maxd = max(1, max(max(row) for row in state.map.dist if row))
    return min(1.0, sum(finite) / len(finite) / maxd)


def extract_features(state: GameState, player: Side) -> list[float]:
    enemy = player.opponent
    my_hq = state.get_hq(player)
    enemy_hq = state.get_hq(enemy)
    my_units = state.units_for(player)
    enemy_units = state.units_for(enemy)
    my_hq_pos = state.player(player).hq_pos
    enemy_hq_pos = state.player(enemy).hq_pos
    my_buildings = state.buildings_for(player)
    enemy_buildings = state.buildings_for(enemy)
    my_income = _income(state, player)
    enemy_income = _income(state, enemy)
    point_control = 0
    for point in state.map.points:
        diff = len(state.get_units_at(point, player)) - len(state.get_units_at(point, enemy))
        if diff > 0:
            point_control += 1
        elif diff < 0:
            point_control -= 1
    nearest_enemy_to_hq = min((state.map.dist[u.pos][my_hq_pos] for u in enemy_units), default=0)
    maxd = max(1, max(max(row) for row in state.map.dist if row))
    return [
        1.0,
        state.day / MAX_DAYS,
        state.player(player).gold / 5000.0,
        state.player(enemy).gold / 5000.0,
        len(my_units) / 50.0,
        len(enemy_units) / 50.0,
        len(my_buildings) / 15.0,
        len(enemy_buildings) / 15.0,
        (my_hq.level if my_hq else 0) / 5.0,
        (enemy_hq.level if enemy_hq else 0) / 5.0,
        (my_hq.hp / max(1, my_hq.max_hp)) if my_hq else 0.0,
        (enemy_hq.hp / max(1, enemy_hq.max_hp)) if enemy_hq else 0.0,
        len(state.get_units_at(my_hq_pos, player)) / 20.0,
        len(state.get_units_at(my_hq_pos, enemy)) / 20.0,
        len(state.get_units_at(enemy_hq_pos, player)) / 20.0,
        len(state.get_units_at(enemy_hq_pos, enemy)) / 20.0,
        my_income / 300.0,
        enemy_income / 300.0,
        (my_income - 2 * len(my_units)) / 300.0,
        (enemy_income - 2 * len(enemy_units)) / 300.0,
        point_control / max(1, len(state.map.points)),
        _avg_distance_to(state, my_units, enemy_hq_pos),
        _avg_distance_to(state, my_units, my_hq_pos),
        min(1.0, nearest_enemy_to_hq / maxd),
        len([u for u in my_units if u.target is None]) / 50.0,
        len([u for u in enemy_units if u.target is None]) / 50.0,
    ]


def _empty(action: CommandAction) -> bool:
    return action.train_n == 0 and not action.upgrades and not action.moves


def _append_move_if_valid(state: GameState, player: Side, action: CommandAction, unit: Unit, target: int) -> CommandAction:
    proposal = CommandAction(action.train_n, list(action.upgrades), [*action.moves, MoveCommand(unit.id, target)])
    return proposal if validate_action(state, player, proposal) else action


def _idle_units(state: GameState, player: Side) -> list[Unit]:
    return [u for u in state.units_for(player) if u.target is None]


def _macro_idle(state: GameState, player: Side) -> CommandAction:
    return CommandAction()


def _macro_train(state: GameState, player: Side) -> CommandAction:
    mask = legal_train_mask(state, player)
    for n in reversed(range(len(mask))):
        if n > 0 and mask[n]:
            action = CommandAction(train_n=n)
            return action if validate_action(state, player, action) else CommandAction()
    return CommandAction()


def _upgrade_score(state: GameState, player: Side, region: int, cost: int | None) -> float:
    if cost is None:
        return -1e9
    b = state.buildings.get(region)
    center = state.map.N // 2
    center_dist = state.map.dist[region][center] if state.map.is_reachable(region, center) else 10**9
    central = 1.0 / (1.0 + center_dist)
    if b is None:
        return 45.0 + 8.0 * central - cost / 1000.0
    if b.kind is BuildingKind.HQ:
        return 70.0 + 8.0 * (1.0 - b.hp / max(1, b.max_hp)) + 3.0 * (5 - b.level) - cost / 1200.0
    return 55.0 + 12.0 * central + 4.0 * (1.0 - b.hp / max(1, b.max_hp)) - cost / 1000.0


def _macro_upgrade(state: GameState, player: Side) -> CommandAction:
    mask, costs = legal_upgrade_mask_and_cost(state, player)
    candidates = [( _upgrade_score(state, player, r, costs[r]), r) for r, ok in enumerate(mask) if ok]
    if not candidates:
        return CommandAction()
    _, region = max(candidates)
    action = CommandAction(upgrades=[region])
    return action if validate_action(state, player, action) else CommandAction()


def _macro_expand(state: GameState, player: Side) -> CommandAction:
    targets = [p for p in state.map.points if p not in state.buildings and not state.has_alive_enemy_at(p, player)]
    units = _idle_units(state, player)
    if not targets or not units:
        return CommandAction()
    center = state.map.N // 2
    targets.sort(key=lambda p: (state.map.dist[p][center], p))
    for target in targets[:5]:
        choices = [u for u in units if state.map.is_reachable(u.pos, target)]
        choices.sort(key=lambda u: (state.map.dist[u.pos][target], u.id.number))
        for unit in choices[:3]:
            action = CommandAction(moves=[MoveCommand(unit.id, target)])
            if validate_action(state, player, action):
                return action
    return CommandAction()


def _multi_move_to(state: GameState, player: Side, target: int, max_units: int) -> CommandAction:
    units = [u for u in _idle_units(state, player) if u.pos != target and state.map.is_reachable(u.pos, target)]
    units.sort(key=lambda u: (state.map.dist[u.pos][target], u.id.number))
    action = CommandAction()
    for unit in units[:max_units]:
        action = _append_move_if_valid(state, player, action, unit, target)
    return action


def _macro_attack_hq(state: GameState, player: Side) -> CommandAction:
    return _multi_move_to(state, player, state.player(player.opponent).hq_pos, 4)


def _macro_defend_hq(state: GameState, player: Side) -> CommandAction:
    return _multi_move_to(state, player, state.player(player).hq_pos, 4)


def _macro_fill_worker(state: GameState, player: Side) -> CommandAction:
    buildings = []
    for region, building in state.buildings_for(player):
        open_slots = building.worker_cap - len(state.get_units_at(region, player))
        if open_slots > 0:
            buildings.append((region, open_slots))
    units = _idle_units(state, player)
    if not buildings or not units:
        return CommandAction()
    best = None
    for region, _slots in buildings:
        for unit in units:
            if unit.pos == region or not state.map.is_reachable(unit.pos, region):
                continue
            item = (state.map.dist[unit.pos][region], unit.id.number, unit, region)
            if best is None or item[:2] < best[:2]:
                best = item
    if best is None:
        return CommandAction()
    _, _, unit, region = best
    action = CommandAction(moves=[MoveCommand(unit.id, region)])
    return action if validate_action(state, player, action) else CommandAction()


def _macro_raid_base(state: GameState, player: Side) -> CommandAction:
    enemy = player.opponent
    targets = [(region, b) for region, b in state.buildings_for(enemy) if b.kind is BuildingKind.BASE]
    if not targets:
        return CommandAction()
    targets.sort(key=lambda rb: (rb[1].hp, state.map.dist[state.player(player).hq_pos][rb[0]], rb[0]))
    return _multi_move_to(state, player, targets[0][0], 3)


_MACRO_BUILDERS = [
    _macro_idle,
    _macro_train,
    _macro_upgrade,
    _macro_expand,
    _macro_attack_hq,
    _macro_defend_hq,
    _macro_fill_worker,
    _macro_raid_base,
]


def macro_action(state: GameState, player: Side, index: int) -> CommandAction:
    return _MACRO_BUILDERS[index](state, player)


def legal_macro_actions(state: GameState, player: Side) -> tuple[list[CommandAction], list[bool]]:
    actions: list[CommandAction] = []
    mask: list[bool] = []
    for i, builder in enumerate(_MACRO_BUILDERS):
        action = builder(state, player)
        legal = validate_action(state, player, action) and (i == 0 or not _empty(action))
        actions.append(action if legal else CommandAction())
        mask.append(legal)
    mask[0] = True
    return actions, mask


def _softmax(logits: list[float], mask: list[bool], temperature: float = 1.0) -> list[float]:
    vals = [logits[i] / max(1e-6, temperature) if mask[i] else -1e30 for i in range(len(logits))]
    m = max(vals)
    exps = [0.0 if v < -1e20 else math.exp(v - m) for v in vals]
    total = sum(exps)
    if total <= 0:
        return [1.0 if i == 0 else 0.0 for i in range(len(logits))]
    return [v / total for v in exps]


def _dot(a: list[float], b: list[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


class SimpleNeuralPolicy:
    def __init__(
        self,
        input_dim: int | None = None,
        hidden_dim: int = 32,
        action_dim: int = len(ACTION_NAMES),
        seed: int | None = None,
    ) -> None:
        self.input_dim = input_dim or 26
        self.hidden_dim = hidden_dim
        self.action_dim = action_dim
        self.rng = random.Random(seed)
        scale = 0.08
        self.w1 = [[self.rng.uniform(-scale, scale) for _ in range(self.input_dim)] for _ in range(hidden_dim)]
        self.b1 = [0.0 for _ in range(hidden_dim)]
        self.w2 = [[self.rng.uniform(-scale, scale) for _ in range(hidden_dim)] for _ in range(action_dim)]
        self.b2 = [-1.25, 0.15, 0.05, 0.20, 0.10, 0.00, 0.05, 0.10][:action_dim]
        self.wv = [self.rng.uniform(-scale, scale) for _ in range(hidden_dim)]
        self.bv = 0.0

    def copy(self) -> "SimpleNeuralPolicy":
        return copy.deepcopy(self)

    def _hidden(self, features: list[float]) -> list[float]:
        return [math.tanh(_dot(row, features) + b) for row, b in zip(self.w1, self.b1)]

    def forward(self, features: list[float], mask: list[bool] | None = None, temperature: float = 1.0):
        if len(features) != self.input_dim:
            raise ValueError(f"expected {self.input_dim} features, got {len(features)}")
        h = self._hidden(features)
        logits = [_dot(row, h) + b for row, b in zip(self.w2, self.b2)]
        if mask is None:
            mask = [True] * self.action_dim
        probs = _softmax(logits, mask, temperature)
        value = _dot(self.wv, h) + self.bv
        entropy = -sum(p * math.log(max(p, 1e-12)) for p in probs if p > 0)
        return h, logits, probs, value, entropy

    def select_action(
        self,
        state: GameState,
        player: Side,
        *,
        deterministic: bool = False,
        temperature: float = 1.0,
        rng: random.Random | None = None,
    ) -> PolicyDecision:
        rng = rng or self.rng
        actions, mask = legal_macro_actions(state, player)
        features = extract_features(state, player)
        effective_mask = list(mask)
        if any(effective_mask[1:]):
            effective_mask[0] = False
        _h, _logits, probs, value, entropy = self.forward(features, effective_mask, temperature)
        if deterministic:
            idx = max(range(self.action_dim), key=lambda i: probs[i] if effective_mask[i] else -1.0)
        else:
            r = rng.random()
            acc = 0.0
            idx = 0
            for i, p in enumerate(probs):
                acc += p
                if r <= acc:
                    idx = i
                    break
        logp = math.log(max(probs[idx], 1e-12))
        return PolicyDecision(actions[idx], idx, features, effective_mask, value, logp, entropy)

    def act(self, state: GameState, player: Side, deterministic: bool = True) -> CommandAction:
        return self.select_action(state, player, deterministic=deterministic).action

    def update_trajectories(
        self,
        trajectories: list[list[dict]],
        *,
        lr: float = 0.01,
        gamma: float = 0.99,
        value_coef: float = 0.5,
        grad_clip: float = 5.0,
    ) -> dict[str, float]:
        samples: list[tuple[dict, float]] = []
        for traj in trajectories:
            ret = 0.0
            returns: list[float] = []
            for step in reversed(traj):
                ret = float(step["reward"]) + gamma * ret
                returns.append(ret)
            returns.reverse()
            samples.extend(zip(traj, returns))
        if not samples:
            return {"samples": 0, "loss": 0.0, "policy_loss": 0.0, "value_loss": 0.0, "avg_return": 0.0}

        gw1 = [[0.0 for _ in range(self.input_dim)] for _ in range(self.hidden_dim)]
        gb1 = [0.0 for _ in range(self.hidden_dim)]
        gw2 = [[0.0 for _ in range(self.hidden_dim)] for _ in range(self.action_dim)]
        gb2 = [0.0 for _ in range(self.action_dim)]
        gwv = [0.0 for _ in range(self.hidden_dim)]
        gbv = 0.0
        policy_loss = 0.0
        value_loss = 0.0
        returns_sum = 0.0

        for step, ret in samples:
            features = step["features"]
            mask = step["legal_mask"]
            action_idx = step["action_index"]
            h, _logits, probs, value, _entropy = self.forward(features, mask)
            advantage = max(-5.0, min(5.0, ret - value))
            returns_sum += ret
            policy_loss += -advantage * math.log(max(probs[action_idx], 1e-12))
            value_loss += 0.5 * (value - ret) * (value - ret)

            dlogits = [0.0 for _ in range(self.action_dim)]
            for k in range(self.action_dim):
                if not mask[k]:
                    continue
                target = 1.0 if k == action_idx else 0.0
                dlogits[k] = advantage * (probs[k] - target)

            dvalue = value_coef * (value - ret)
            dh = [dvalue * self.wv[j] for j in range(self.hidden_dim)]
            for k in range(self.action_dim):
                gb2[k] += dlogits[k]
                for j in range(self.hidden_dim):
                    gw2[k][j] += dlogits[k] * h[j]
                    dh[j] += dlogits[k] * self.w2[k][j]
            gbv += dvalue
            for j in range(self.hidden_dim):
                gwv[j] += dvalue * h[j]
                dz = dh[j] * (1.0 - h[j] * h[j])
                gb1[j] += dz
                for i in range(self.input_dim):
                    gw1[j][i] += dz * features[i]

        n = float(len(samples))
        grads: list[float] = []
        for row in gw1: grads.extend(row)
        grads.extend(gb1)
        for row in gw2: grads.extend(row)
        grads.extend(gb2)
        grads.extend(gwv)
        grads.append(gbv)
        norm = math.sqrt(sum(g * g for g in grads)) / n
        scale = 1.0 if norm <= grad_clip else grad_clip / max(norm, 1e-12)
        step = lr * scale / n

        for j in range(self.hidden_dim):
            self.b1[j] -= step * gb1[j]
            self.wv[j] -= step * gwv[j]
            for i in range(self.input_dim):
                self.w1[j][i] -= step * gw1[j][i]
        self.bv -= step * gbv
        for k in range(self.action_dim):
            self.b2[k] -= step * gb2[k]
            for j in range(self.hidden_dim):
                self.w2[k][j] -= step * gw2[k][j]

        return {
            "samples": len(samples),
            "loss": policy_loss / n + value_coef * value_loss / n,
            "policy_loss": policy_loss / n,
            "value_loss": value_loss / n,
            "avg_return": returns_sum / n,
            "grad_norm": norm,
        }

    def to_dict(self) -> dict:
        return {
            "type": "SimpleNeuralPolicy",
            "input_dim": self.input_dim,
            "hidden_dim": self.hidden_dim,
            "action_dim": self.action_dim,
            "action_names": ACTION_NAMES,
            "w1": self.w1,
            "b1": self.b1,
            "w2": self.w2,
            "b2": self.b2,
            "wv": self.wv,
            "bv": self.bv,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "SimpleNeuralPolicy":
        obj = cls(data["input_dim"], data["hidden_dim"], data["action_dim"])
        obj.w1 = data["w1"]
        obj.b1 = data["b1"]
        obj.w2 = data["w2"]
        obj.b2 = data["b2"]
        obj.wv = data["wv"]
        obj.bv = data["bv"]
        return obj

    def save(self, path: str | Path, extra: dict | None = None) -> None:
        payload = self.to_dict()
        if extra:
            payload["extra"] = extra
        Path(path).write_text(json.dumps(payload, indent=2), encoding="utf-8")

    @classmethod
    def load(cls, path: str | Path) -> "SimpleNeuralPolicy":
        return cls.from_dict(json.loads(Path(path).read_text(encoding="utf-8")))

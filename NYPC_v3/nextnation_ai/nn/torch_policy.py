"""PyTorch GNN actor-critic and hierarchical action distribution."""

from __future__ import annotations

import sys
from dataclasses import dataclass

import torch
from torch import nn
from torch.distributions import Categorical

from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.constants import MOVE_COST, TRAIN_COST
from nextnation_ai.game.legal import (
    legal_move_origins,
    legal_move_target_mask,
    legal_train_mask,
    legal_upgrade_mask_and_cost,
    move_command_cost,
    validate_action,
)
from nextnation_ai.game.observation import build_observation
from nextnation_ai.game.state import GameState, Side, UnitId


MAX_UPGRADES_PER_TURN = 2
MAX_MOVE_GROUPS = 3
MAX_MOVE_COUNT = 4
MASK_NEG = -1.0e9


@dataclass(frozen=True)
class MoveComponent:
    origin: int
    target: int
    count: int


@dataclass(frozen=True)
class ActionComponents:
    train_n: int
    upgrades: tuple[int, ...] = ()
    moves: tuple[MoveComponent, ...] = ()

    @property
    def upgrade(self) -> int | None:
        return self.upgrades[0] if self.upgrades else None

    @property
    def move_origin(self) -> int | None:
        return self.moves[0].origin if self.moves else None

    @property
    def move_target(self) -> int | None:
        return self.moves[0].target if self.moves else None

    @property
    def move_count(self) -> int:
        return self.moves[0].count if self.moves else 0


@dataclass
class PolicyStep:
    action: CommandAction
    components: ActionComponents
    logp: torch.Tensor
    entropy: torch.Tensor
    value: torch.Tensor
    features: dict[str, torch.Tensor]
    # True when the sampled components failed validation and were degraded;
    # logp/entropy are recomputed for the degraded components so PPO ratios
    # stay consistent. With correct masks this should never happen.
    fallback: bool = False


# Number of times select_action had to degrade a sampled action. Nonzero means
# a legal-mask bug; kept as a module counter so trainers can assert on it.
_FALLBACK_COUNT = 0


def fallback_count() -> int:
    return _FALLBACK_COUNT


@dataclass
class EvaluatedAction:
    logp: torch.Tensor
    entropy: torch.Tensor
    value: torch.Tensor
    imitation_loss: torch.Tensor | None = None


class GraphEncoder(nn.Module):
    def __init__(self, node_dim: int, edge_dim: int, global_dim: int, hidden_dim: int = 128, layers: int = 4):
        super().__init__()
        self.node_proj = nn.Linear(node_dim, hidden_dim)
        self.edge_proj = nn.Linear(edge_dim, hidden_dim)
        self.global_proj = nn.Sequential(nn.Linear(global_dim, hidden_dim), nn.ReLU(), nn.Linear(hidden_dim, hidden_dim))
        self.layers = nn.ModuleList()
        for _ in range(layers):
            self.layers.append(
                nn.Sequential(
                    nn.LayerNorm(hidden_dim),
                    nn.Linear(hidden_dim, hidden_dim),
                    nn.ReLU(),
                    nn.Linear(hidden_dim, hidden_dim),
                )
            )
        self.out_norm = nn.LayerNorm(hidden_dim)

    def forward(
        self,
        x: torch.Tensor,
        edge_index: torch.Tensor,
        edge_attr: torch.Tensor,
        global_feat: torch.Tensor,
        batch: torch.Tensor | None = None,
        num_graphs: int = 1,
    ):
        h = self.node_proj(x)
        e = self.edge_proj(edge_attr)
        src, dst = edge_index[0], edge_index[1]
        deg = torch.zeros(h.size(0), dtype=h.dtype, device=h.device)
        deg.index_add_(0, dst, torch.ones(dst.numel(), dtype=h.dtype, device=h.device))
        deg = deg.clamp_min(1.0).unsqueeze(-1)
        for layer in self.layers:
            msg = h[src] + e
            agg = torch.zeros_like(h)
            agg.index_add_(0, dst, msg)
            agg = agg / deg
            h = h + layer(agg)
        h = self.out_norm(h)
        if batch is None:
            graph = h.mean(dim=0) + self.global_proj(global_feat)
        else:
            # Disjoint graphs share no edges, so message passing above is
            # already correct on the concatenated batch; only pooling is
            # per-graph.
            counts = torch.zeros(num_graphs, dtype=h.dtype, device=h.device)
            counts.index_add_(0, batch, torch.ones(batch.numel(), dtype=h.dtype, device=h.device))
            mean = torch.zeros(num_graphs, h.size(1), dtype=h.dtype, device=h.device)
            mean.index_add_(0, batch, h)
            mean = mean / counts.clamp_min(1.0).unsqueeze(-1)
            graph = mean + self.global_proj(global_feat)
        return h, graph


class NextNationGNNPolicy(nn.Module):
    def __init__(self, node_dim: int = 39, edge_dim: int = 5, global_dim: int = 12, hidden_dim: int = 128, layers: int = 4):
        super().__init__()
        self.encoder = GraphEncoder(node_dim, edge_dim, global_dim, hidden_dim, layers)
        self.train_head = nn.Linear(hidden_dim, 4)
        self.upgrade_head = nn.Linear(hidden_dim, 1)
        self.upgrade_noop = nn.Linear(hidden_dim, 1)
        self.move_origin_head = nn.Linear(hidden_dim, 1)
        self.move_origin_noop = nn.Linear(hidden_dim, 1)
        self.move_target_head = nn.Linear(hidden_dim, 1)
        self.move_count_head = nn.Linear(hidden_dim, MAX_MOVE_COUNT + 1)
        self.value_head = nn.Sequential(nn.Linear(hidden_dim, hidden_dim), nn.ReLU(), nn.Linear(hidden_dim, 1))

    def forward(self, features: dict[str, torch.Tensor]) -> dict[str, torch.Tensor]:
        node_h, graph_h = self.encoder(features["x"], features["edge_index"], features["edge_attr"], features["global_feat"])
        return {
            "node_h": node_h,
            "graph_h": graph_h,
            "train_logits": self.train_head(graph_h),
            "upgrade_logits": self.upgrade_head(node_h).squeeze(-1),
            "upgrade_noop": self.upgrade_noop(graph_h).squeeze(-1),
            "move_origin_logits": self.move_origin_head(node_h).squeeze(-1),
            "move_origin_noop": self.move_origin_noop(graph_h).squeeze(-1),
            "move_target_logits": self.move_target_head(node_h).squeeze(-1),
            "move_count_logits": self.move_count_head(graph_h),
            "value": self.value_head(graph_h).squeeze(-1),
        }

    def forward_batched(self, features: dict[str, torch.Tensor]) -> dict[str, torch.Tensor]:
        """Forward over B disjoint graphs concatenated PyG-style.

        Node-level outputs stay flat ([total_nodes]); graph-level outputs are
        [B, ...]. ``features`` needs ``batch_index`` ([total_nodes] graph id)
        and ``num_graphs`` in addition to the single-graph keys.
        """
        node_h, graph_h = self.encoder(
            features["x"],
            features["edge_index"],
            features["edge_attr"],
            features["global_feat"],
            batch=features["batch_index"],
            num_graphs=int(features["num_graphs"]),
        )
        return {
            "node_h": node_h,
            "graph_h": graph_h,
            "train_logits": self.train_head(graph_h),
            "upgrade_logits": self.upgrade_head(node_h).squeeze(-1),
            "upgrade_noop": self.upgrade_noop(graph_h).squeeze(-1),
            "move_origin_logits": self.move_origin_head(node_h).squeeze(-1),
            "move_origin_noop": self.move_origin_noop(graph_h).squeeze(-1),
            "move_target_logits": self.move_target_head(node_h).squeeze(-1),
            "move_count_logits": self.move_count_head(graph_h),
            "value": self.value_head(graph_h).squeeze(-1),
        }


def state_to_tensors(state: GameState, player: Side, device: torch.device) -> dict[str, torch.Tensor]:
    obs = build_observation(state, player)
    x = torch.tensor(obs.node_features, dtype=torch.float32, device=device)
    edge_index = torch.tensor(obs.edge_index, dtype=torch.long, device=device).t().contiguous()
    edge_attr = torch.tensor(obs.edge_features, dtype=torch.float32, device=device)
    global_feat = torch.tensor(obs.global_features, dtype=torch.float32, device=device)
    return {"x": x, "edge_index": edge_index, "edge_attr": edge_attr, "global_feat": global_feat}


def _mask_tensor(mask: list[bool], device: torch.device) -> torch.Tensor:
    return torch.tensor(mask, dtype=torch.bool, device=device)


def _masked_categorical(logits: torch.Tensor, mask: torch.Tensor) -> Categorical:
    if not bool(mask.any()):
        mask = torch.zeros_like(mask, dtype=torch.bool)
        mask[0] = True
    masked = logits.masked_fill(~mask, MASK_NEG)
    return Categorical(logits=masked)


def _append_imitation_ce(terms: list[torch.Tensor], dist: Categorical, index: int, device: torch.device) -> bool:
    if index < 0 or index >= dist.logits.numel():
        return False
    idx = torch.tensor(index, dtype=torch.long, device=device)
    logp = dist.log_prob(idx)
    if not bool(torch.isfinite(logp).detach().cpu()):
        return False
    if float(logp.detach().cpu()) <= MASK_NEG / 2:
        return False
    terms.append(-logp)
    return True


def _sample_or_argmax(dist: Categorical, deterministic: bool) -> torch.Tensor:
    if deterministic:
        return torch.argmax(dist.logits)
    return dist.sample()


def _legal_upgrade_mask_after_train(
    state: GameState,
    player: Side,
    train_n: int,
    budget: int | None = None,
    selected: set[int] | None = None,
):
    reserved = train_n * TRAIN_COST
    mask, costs = legal_upgrade_mask_and_cost(state, player, reserved_gold=reserved, already_selected=selected)
    if budget is not None:
        mask = [ok and costs[i] is not None and int(costs[i]) <= budget for i, ok in enumerate(mask)]
    return mask, costs


def _idle_units_for_move(state: GameState, player: Side, origin: int, used_units: set[UnitId] | None = None):
    used_units = used_units or set()
    return [unit for unit in state.get_idle_units_at(origin, player) if unit.id not in used_units]


def _build_action_from_components(state: GameState, player: Side, comp: ActionComponents) -> CommandAction:
    """Decode components into a CommandAction without validating.

    Callers must validate the result; a silent fallback here would let the
    executed action diverge from the components used for log-prob evaluation.
    """
    moves: list[MoveCommand] = []
    used: set[UnitId] = set()
    for move in comp.moves[:MAX_MOVE_GROUPS]:
        if move.count <= 0:
            continue
        units = _idle_units_for_move(state, player, move.origin, used)
        for unit in units[: min(MAX_MOVE_COUNT, move.count)]:
            used.add(unit.id)
            moves.append(MoveCommand(unit.id, move.target))
    return CommandAction(comp.train_n, list(comp.upgrades[:MAX_UPGRADES_PER_TURN]), moves)


def _components_from_action(state: GameState, player: Side, action: CommandAction) -> ActionComponents:
    train_n = max(0, min(3, int(action.train_n)))
    train_mask = legal_train_mask(state, player)
    if train_n >= len(train_mask) or not train_mask[train_n]:
        train_n = 0

    upgrades: list[int] = []
    for region in action.upgrades:
        if region not in upgrades:
            upgrades.append(region)
        if len(upgrades) >= MAX_UPGRADES_PER_TURN:
            break

    groups: dict[tuple[int, int], int] = {}
    for move in action.moves:
        unit = state.units.get(move.unit_id)
        if unit is None or unit.owner is not player:
            continue
        key = (unit.pos, move.target)
        groups[key] = groups.get(key, 0) + 1
    move_components = [
        MoveComponent(origin, target, min(MAX_MOVE_COUNT, count))
        for (origin, target), count in sorted(groups.items(), key=lambda item: (-item[1], item[0][0], item[0][1]))
    ][:MAX_MOVE_GROUPS]

    comp = ActionComponents(train_n, tuple(upgrades), tuple(move_components))
    if validate_action(state, player, _build_action_from_components(state, player, comp)):
        return comp
    return ActionComponents(train_n)


def components_from_manual_action(state: GameState, player: Side, action: CommandAction) -> ActionComponents:
    return _components_from_action(state, player, action)


def _dist_train(out: dict[str, torch.Tensor], state: GameState, player: Side):
    mask = _mask_tensor(legal_train_mask(state, player), out["train_logits"].device)
    return _masked_categorical(out["train_logits"], mask)


def _dist_upgrade(
    out: dict[str, torch.Tensor],
    state: GameState,
    player: Side,
    train_n: int,
    budget: int | None = None,
    selected: set[int] | None = None,
):
    mask, costs = _legal_upgrade_mask_after_train(state, player, train_n, budget=budget, selected=selected)
    logits = torch.cat([out["upgrade_noop"].view(1), out["upgrade_logits"]])
    full_mask = _mask_tensor([True, *mask], logits.device)
    return _masked_categorical(logits, full_mask), costs


def _origin_mask_list(state: GameState, player: Side, budget: int, used_units: set[UnitId] | None = None) -> list[bool]:
    """[noop, node 0..N-1] origin mask, shared by the sampler and the batched trace."""
    used_units = used_units or set()
    origins = set(legal_move_origins(state, player))
    mask = [False] * state.map.N
    for origin in origins:
        if not _idle_units_for_move(state, player, origin, used_units):
            continue
        target_mask = legal_move_target_mask(state, player, origin, budget=budget)
        if 0 <= origin < len(target_mask):
            target_mask[origin] = False
        if any(target_mask):
            mask[origin] = True
    return [True, *mask]


def _target_mask_list(state: GameState, player: Side, origin: int, budget: int) -> list[bool]:
    mask = legal_move_target_mask(state, player, origin, budget=budget)
    if 0 <= origin < len(mask):
        mask[origin] = False
    return mask


def _count_mask_list(
    state: GameState,
    player: Side,
    origin: int,
    target: int,
    budget: int,
    used_units: set[UnitId] | None = None,
) -> list[bool]:
    units = _idle_units_for_move(state, player, origin, used_units)
    mask = [False] * (MAX_MOVE_COUNT + 1)
    if target < 0 or target >= state.map.N or not state.map.is_reachable(origin, target):
        mask[0] = True
        return mask
    cost = move_command_cost(state, player, target)
    max_count = len(units) if cost == 0 else min(len(units), max(0, budget) // cost)
    for i in range(1, min(MAX_MOVE_COUNT, max_count) + 1):
        mask[i] = True
    return mask


def _dist_origin(out: dict[str, torch.Tensor], state: GameState, player: Side, budget: int, used_units: set[UnitId] | None = None):
    logits = torch.cat([out["move_origin_noop"].view(1), out["move_origin_logits"]])
    full_mask = _mask_tensor(_origin_mask_list(state, player, budget, used_units), logits.device)
    return _masked_categorical(logits, full_mask)


def _dist_target(out: dict[str, torch.Tensor], state: GameState, player: Side, origin: int, budget: int):
    mask = _target_mask_list(state, player, origin, budget)
    return _masked_categorical(out["move_target_logits"], _mask_tensor(mask, out["move_target_logits"].device))


def _dist_count(
    out: dict[str, torch.Tensor],
    state: GameState,
    player: Side,
    origin: int,
    target: int,
    budget: int,
    used_units: set[UnitId] | None = None,
):
    mask = _count_mask_list(state, player, origin, target, budget, used_units)
    return _masked_categorical(out["move_count_logits"], _mask_tensor(mask, out["move_count_logits"].device))


def _record_dist_choice(logps: list[torch.Tensor], entropies: list[torch.Tensor], dist: Categorical, idx: torch.Tensor) -> None:
    logps.append(dist.log_prob(idx))
    entropies.append(dist.entropy())


def _select_components(out: dict[str, torch.Tensor], state: GameState, player: Side, deterministic: bool):
    logps: list[torch.Tensor] = []
    entropies: list[torch.Tensor] = []

    train_dist = _dist_train(out, state, player)
    train_idx = _sample_or_argmax(train_dist, deterministic)
    train_n = int(train_idx.item())
    _record_dist_choice(logps, entropies, train_dist, train_idx)

    budget = state.player(player).gold - train_n * TRAIN_COST
    if budget < 0:
        train_n = 0
        budget = state.player(player).gold

    upgrades: list[int] = []
    selected_upgrades: set[int] = set()
    for _ in range(MAX_UPGRADES_PER_TURN):
        upgrade_dist, costs = _dist_upgrade(out, state, player, train_n, budget=budget, selected=selected_upgrades)
        upgrade_idx = _sample_or_argmax(upgrade_dist, deterministic)
        _record_dist_choice(logps, entropies, upgrade_dist, upgrade_idx)
        if int(upgrade_idx.item()) == 0:
            break
        region = int(upgrade_idx.item()) - 1
        cost = costs[region]
        if cost is None or int(cost) > budget:
            break
        upgrades.append(region)
        selected_upgrades.add(region)
        budget -= int(cost)

    move_components: list[MoveComponent] = []
    used_units: set[UnitId] = set()
    for _ in range(MAX_MOVE_GROUPS):
        origin_dist = _dist_origin(out, state, player, budget=max(0, budget), used_units=used_units)
        origin_idx = _sample_or_argmax(origin_dist, deterministic)
        _record_dist_choice(logps, entropies, origin_dist, origin_idx)
        if int(origin_idx.item()) == 0:
            break
        origin = int(origin_idx.item()) - 1

        target_dist = _dist_target(out, state, player, origin, max(0, budget))
        target_idx = _sample_or_argmax(target_dist, deterministic)
        _record_dist_choice(logps, entropies, target_dist, target_idx)
        target = int(target_idx.item())

        count_dist = _dist_count(out, state, player, origin, target, max(0, budget), used_units=used_units)
        count_idx = _sample_or_argmax(count_dist, deterministic)
        _record_dist_choice(logps, entropies, count_dist, count_idx)
        count = int(count_idx.item())
        if count <= 0:
            break

        move_components.append(MoveComponent(origin, target, count))
        for unit in _idle_units_for_move(state, player, origin, used_units)[:count]:
            used_units.add(unit.id)
        budget -= move_command_cost(state, player, target) * count

    return ActionComponents(train_n, tuple(upgrades), tuple(move_components)), logps, entropies


def _is_empty_action(action: CommandAction) -> bool:
    return action.train_n == 0 and not action.upgrades and not action.moves


def _activity_count(action: CommandAction) -> int:
    return int(action.train_n > 0) + len(action.upgrades) + len(action.moves)


def _target_priority(state: GameState, player: Side, origin: int, target: int) -> tuple[float, int]:
    enemy_hq = state.player(player.opponent).hq_pos
    own_hq = state.player(player).hq_pos
    enemy_building = state.buildings.get(target)
    stronghold_bonus = 1.25 if target in state.map.points else 0.0
    enemy_bonus = 1.5 if enemy_building is not None and enemy_building.owner is player.opponent else 0.0
    own_penalty = -0.75 if target == own_hq else 0.0
    before = state.map.dist[origin][enemy_hq] if state.map.dist else 0
    after = state.map.dist[target][enemy_hq] if state.map.dist else before
    travel = state.map.dist[origin][target] if state.map.dist else 0
    finite = [d for row in state.map.dist for d in row if d < 10**20]
    max_dist = max(finite) if finite else 1
    progress = 0.0 if before >= 10**20 or after >= 10**20 else (before - after) / max(1.0, float(before))
    distance_penalty = 0.85 * min(1.0, travel / max(1.0, float(max_dist)))
    score = 1.75 * progress + stronghold_bonus + enemy_bonus + own_penalty - distance_penalty
    return score, -target


def _nonempty_fallback_action(state: GameState, player: Side) -> CommandAction:
    train_options = [n for n, ok in enumerate(legal_train_mask(state, player)) if ok]
    train_n = max(train_options) if train_options else 0
    action = CommandAction(train_n=train_n)
    if not validate_action(state, player, action):
        action = CommandAction()

    upgrade_mask, _ = legal_upgrade_mask_and_cost(state, player, reserved_gold=action.train_n * TRAIN_COST)
    for region, ok in sorted(enumerate(upgrade_mask), key=lambda item: (item[0] not in state.map.points, item[0])):
        if len(action.upgrades) >= MAX_UPGRADES_PER_TURN:
            break
        if not ok:
            continue
        proposal = CommandAction(action.train_n, [*action.upgrades, region], list(action.moves))
        if validate_action(state, player, proposal):
            action = proposal

    used: set[UnitId] = {move.unit_id for move in action.moves}
    for origin in legal_move_origins(state, player):
        if len({(state.units[m.unit_id].pos, m.target) for m in action.moves if m.unit_id in state.units}) >= MAX_MOVE_GROUPS:
            break
        units = [u for u in state.get_idle_units_at(origin, player) if u.id not in used]
        if not units:
            continue
        target_mask = legal_move_target_mask(state, player, origin)
        if 0 <= origin < len(target_mask):
            target_mask[origin] = False
        legal_targets = [target for target, ok in enumerate(target_mask) if ok]
        strategic_targets = set(state.map.points)
        for region, building in state.buildings.items():
            if building.owner is not player:
                strategic_targets.add(region)
        targets = [target for target in legal_targets if target in strategic_targets]
        if not targets:
            targets = legal_targets
        targets.sort(key=lambda target: _target_priority(state, player, origin, target), reverse=True)
        for target in targets[:8]:
            for count in range(min(MAX_MOVE_COUNT, len(units)), 0, -1):
                proposal = CommandAction(
                    action.train_n,
                    list(action.upgrades),
                    [*action.moves, *[MoveCommand(unit.id, target) for unit in units[:count]]],
                )
                if validate_action(state, player, proposal):
                    action = proposal
                    used.update(unit.id for unit in units[:count])
                    break
            else:
                continue
            break

    return action if validate_action(state, player, action) else CommandAction()


def select_action(model: NextNationGNNPolicy, state: GameState, player: Side, device: torch.device, deterministic: bool = False) -> PolicyStep:
    global _FALLBACK_COUNT
    features = state_to_tensors(state, player, device)
    out = model(features)
    comp, logps, entropies = _select_components(out, state, player, deterministic)
    action = _build_action_from_components(state, player, comp)
    logp = torch.stack(logps).sum()
    entropy = torch.stack(entropies).sum()
    fell_back = False
    if not validate_action(state, player, action):
        # Should be unreachable when the legal masks are correct. Degrade to a
        # train-only action and recompute logp/entropy for the degraded
        # components so old_logp matches what evaluate_components will score.
        _FALLBACK_COUNT += 1
        fell_back = True
        print(
            f"torch_policy: sampled action failed validation "
            f"(day={state.day} player={player.word} count={_FALLBACK_COUNT}); degrading to train-only",
            file=sys.stderr,
        )
        train_mask = legal_train_mask(state, player)
        train_n = comp.train_n if 0 <= comp.train_n < len(train_mask) and train_mask[comp.train_n] else 0
        comp = ActionComponents(train_n)
        action = _build_action_from_components(state, player, comp)
        if not validate_action(state, player, action):
            comp = ActionComponents(0)
            action = CommandAction()
        logp, entropy = _evaluate_logp_entropy_for_components(out, state, player, comp, device)
    if deterministic and _activity_count(action) <= 1:
        fallback = _nonempty_fallback_action(state, player)
        if _activity_count(fallback) > _activity_count(action):
            action = fallback
    return PolicyStep(action, comp, logp, entropy, out["value"], features, fallback=fell_back)


def _evaluate_logp_entropy_for_components(
    out: dict[str, torch.Tensor],
    state: GameState,
    player: Side,
    comp: ActionComponents,
    device: torch.device,
) -> tuple[torch.Tensor, torch.Tensor]:
    logps: list[torch.Tensor] = []
    entropies: list[torch.Tensor] = []

    train_dist = _dist_train(out, state, player)
    train_idx = torch.tensor(comp.train_n, dtype=torch.long, device=device)
    _record_dist_choice(logps, entropies, train_dist, train_idx)

    budget = state.player(player).gold - comp.train_n * TRAIN_COST
    selected_upgrades: set[int] = set()
    for i in range(MAX_UPGRADES_PER_TURN):
        upgrade_dist, costs = _dist_upgrade(out, state, player, comp.train_n, budget=budget, selected=selected_upgrades)
        if i < len(comp.upgrades):
            region = comp.upgrades[i]
            upgrade_idx = torch.tensor(region + 1, dtype=torch.long, device=device)
            _record_dist_choice(logps, entropies, upgrade_dist, upgrade_idx)
            if 0 <= region < len(costs) and costs[region] is not None:
                selected_upgrades.add(region)
                budget -= int(costs[region])
        else:
            _record_dist_choice(logps, entropies, upgrade_dist, torch.tensor(0, dtype=torch.long, device=device))
            break

    used_units: set[UnitId] = set()
    for i in range(MAX_MOVE_GROUPS):
        origin_dist = _dist_origin(out, state, player, budget=max(0, budget), used_units=used_units)
        if i >= len(comp.moves):
            _record_dist_choice(logps, entropies, origin_dist, torch.tensor(0, dtype=torch.long, device=device))
            break
        move = comp.moves[i]
        origin_idx = torch.tensor(move.origin + 1, dtype=torch.long, device=device)
        _record_dist_choice(logps, entropies, origin_dist, origin_idx)

        target_dist = _dist_target(out, state, player, move.origin, max(0, budget))
        target_idx = torch.tensor(move.target, dtype=torch.long, device=device)
        _record_dist_choice(logps, entropies, target_dist, target_idx)

        count_dist = _dist_count(out, state, player, move.origin, move.target, max(0, budget), used_units=used_units)
        count_idx = torch.tensor(move.count, dtype=torch.long, device=device)
        _record_dist_choice(logps, entropies, count_dist, count_idx)
        for unit in _idle_units_for_move(state, player, move.origin, used_units)[:move.count]:
            used_units.add(unit.id)
        budget -= move_command_cost(state, player, move.target) * move.count

    return torch.stack(logps).sum(), torch.stack(entropies).sum()


def _append_imitation_for_components(
    ce_terms: list[torch.Tensor],
    out: dict[str, torch.Tensor],
    state: GameState,
    player: Side,
    comp: ActionComponents,
    device: torch.device,
) -> None:
    train_dist = _dist_train(out, state, player)
    train_mask = legal_train_mask(state, player)
    train_n = comp.train_n if 0 <= comp.train_n < len(train_mask) and train_mask[comp.train_n] else 0
    _append_imitation_ce(ce_terms, train_dist, train_n, device)

    budget = state.player(player).gold - train_n * TRAIN_COST
    selected_upgrades: set[int] = set()
    for i in range(MAX_UPGRADES_PER_TURN):
        upgrade_dist, costs = _dist_upgrade(out, state, player, train_n, budget=budget, selected=selected_upgrades)
        if i >= len(comp.upgrades):
            _append_imitation_ce(ce_terms, upgrade_dist, 0, device)
            break
        region = comp.upgrades[i]
        legal = _append_imitation_ce(ce_terms, upgrade_dist, region + 1, device)
        if not legal or not (0 <= region < len(costs)) or costs[region] is None:
            break
        selected_upgrades.add(region)
        budget -= int(costs[region])

    used_units: set[UnitId] = set()
    for i in range(MAX_MOVE_GROUPS):
        origin_dist = _dist_origin(out, state, player, budget=max(0, budget), used_units=used_units)
        if i >= len(comp.moves):
            _append_imitation_ce(ce_terms, origin_dist, 0, device)
            break
        move = comp.moves[i]
        origin_legal = _append_imitation_ce(ce_terms, origin_dist, move.origin + 1, device)
        if not origin_legal:
            break
        target_dist = _dist_target(out, state, player, move.origin, max(0, budget))
        target_legal = _append_imitation_ce(ce_terms, target_dist, move.target, device)
        if not target_legal:
            break
        count_dist = _dist_count(out, state, player, move.origin, move.target, max(0, budget), used_units=used_units)
        count_legal = _append_imitation_ce(ce_terms, count_dist, move.count, device)
        if not count_legal:
            break
        for unit in _idle_units_for_move(state, player, move.origin, used_units)[:move.count]:
            used_units.add(unit.id)
        budget -= move_command_cost(state, player, move.target) * move.count


def evaluate_components(model: NextNationGNNPolicy, state: GameState, player: Side, comp: ActionComponents, device: torch.device, manual_comp: ActionComponents | None = None) -> EvaluatedAction:
    features = state_to_tensors(state, player, device)
    out = model(features)
    logp, entropy = _evaluate_logp_entropy_for_components(out, state, player, comp, device)
    ce_terms: list[torch.Tensor] = []
    if manual_comp is not None:
        _append_imitation_for_components(ce_terms, out, state, player, manual_comp, device)
    imitation = torch.stack(ce_terms).mean() if ce_terms else None
    return EvaluatedAction(logp, entropy, out["value"], imitation)


def action_components_from_action(state: GameState, player: Side, action: CommandAction) -> ActionComponents:
    return _components_from_action(state, player, action)


# ---------------------------------------------------------------------------
# Batched PPO evaluation.
#
# The masks and chosen indices of a stored action depend only on
# (state, player, components) — not on the model — so they can be traced once
# ("slot plan") and reused across PPO epochs, and the per-slot log-probs can
# then be computed as vectorized masked log-softmaxes over a padded batch.
# The traces below must mirror _evaluate_logp_entropy_for_components and
# _append_imitation_for_components exactly; test_torch_policy_consistency
# asserts the equivalence against the sequential path.
# ---------------------------------------------------------------------------

Slot = tuple[list[bool], int]  # (effective mask, chosen index)


@dataclass
class SlotPlan:
    n: int  # map size, for padding node-sized slots
    train: list[Slot]  # mask length 4
    upgrades: list[Slot]  # mask length N + 1 (index 0 = noop)
    origins: list[Slot]  # mask length N + 1 (index 0 = noop)
    targets: list[Slot]  # mask length N
    counts: list[Slot]  # mask length MAX_MOVE_COUNT + 1

    @property
    def num_terms(self) -> int:
        return len(self.train) + len(self.upgrades) + len(self.origins) + len(self.targets) + len(self.counts)


def _effective_mask(mask: list[bool]) -> list[bool]:
    # Mirror of _masked_categorical's all-false fallback (force index 0).
    if any(mask):
        return list(mask)
    forced = [False] * len(mask)
    forced[0] = True
    return forced


def component_slot_plan(state: GameState, player: Side, comp: ActionComponents) -> SlotPlan:
    """Trace of _evaluate_logp_entropy_for_components: one (mask, index) per recorded slot."""
    train_slot = (_effective_mask(legal_train_mask(state, player)), comp.train_n)

    budget = state.player(player).gold - comp.train_n * TRAIN_COST
    upgrades: list[Slot] = []
    selected_upgrades: set[int] = set()
    for i in range(MAX_UPGRADES_PER_TURN):
        mask, costs = _legal_upgrade_mask_after_train(state, player, comp.train_n, budget=budget, selected=selected_upgrades)
        full = _effective_mask([True, *mask])
        if i < len(comp.upgrades):
            region = comp.upgrades[i]
            upgrades.append((full, region + 1))
            if 0 <= region < len(costs) and costs[region] is not None:
                selected_upgrades.add(region)
                budget -= int(costs[region])
        else:
            upgrades.append((full, 0))
            break

    origins: list[Slot] = []
    targets: list[Slot] = []
    counts: list[Slot] = []
    used_units: set[UnitId] = set()
    for i in range(MAX_MOVE_GROUPS):
        origin_mask = _effective_mask(_origin_mask_list(state, player, max(0, budget), used_units))
        if i >= len(comp.moves):
            origins.append((origin_mask, 0))
            break
        move = comp.moves[i]
        origins.append((origin_mask, move.origin + 1))
        targets.append((_effective_mask(_target_mask_list(state, player, move.origin, max(0, budget))), move.target))
        counts.append(
            (_effective_mask(_count_mask_list(state, player, move.origin, move.target, max(0, budget), used_units)), move.count)
        )
        for unit in _idle_units_for_move(state, player, move.origin, used_units)[: move.count]:
            used_units.add(unit.id)
        budget -= move_command_cost(state, player, move.target) * move.count

    return SlotPlan(state.map.N, [train_slot], upgrades, origins, targets, counts)


def imitation_slot_plan(state: GameState, player: Side, comp: ActionComponents) -> SlotPlan:
    """Trace of _append_imitation_for_components: slots for the appended CE terms only."""
    n = state.map.N
    train: list[Slot] = []
    train_mask_raw = legal_train_mask(state, player)
    train_n = comp.train_n if 0 <= comp.train_n < len(train_mask_raw) and train_mask_raw[comp.train_n] else 0
    train_mask = _effective_mask(train_mask_raw)
    if 0 <= train_n < len(train_mask) and train_mask[train_n]:
        train.append((train_mask, train_n))

    budget = state.player(player).gold - train_n * TRAIN_COST
    upgrades: list[Slot] = []
    selected_upgrades: set[int] = set()
    for i in range(MAX_UPGRADES_PER_TURN):
        mask, costs = _legal_upgrade_mask_after_train(state, player, train_n, budget=budget, selected=selected_upgrades)
        full = _effective_mask([True, *mask])
        if i >= len(comp.upgrades):
            if full[0]:
                upgrades.append((full, 0))
            break
        region = comp.upgrades[i]
        legal = 0 <= region + 1 < len(full) and full[region + 1]
        if legal:
            upgrades.append((full, region + 1))
        if not legal or not (0 <= region < len(costs)) or costs[region] is None:
            break
        selected_upgrades.add(region)
        budget -= int(costs[region])

    origins: list[Slot] = []
    targets: list[Slot] = []
    counts: list[Slot] = []
    used_units: set[UnitId] = set()
    for i in range(MAX_MOVE_GROUPS):
        origin_mask = _effective_mask(_origin_mask_list(state, player, max(0, budget), used_units))
        if i >= len(comp.moves):
            if origin_mask[0]:
                origins.append((origin_mask, 0))
            break
        move = comp.moves[i]
        origin_legal = 0 <= move.origin + 1 < len(origin_mask) and origin_mask[move.origin + 1]
        if not origin_legal:
            break
        origins.append((origin_mask, move.origin + 1))
        target_mask = _effective_mask(_target_mask_list(state, player, move.origin, max(0, budget)))
        target_legal = 0 <= move.target < len(target_mask) and target_mask[move.target]
        if not target_legal:
            break
        targets.append((target_mask, move.target))
        count_mask = _effective_mask(_count_mask_list(state, player, move.origin, move.target, max(0, budget), used_units))
        count_legal = 0 <= move.count < len(count_mask) and count_mask[move.count]
        if not count_legal:
            break
        counts.append((count_mask, move.count))
        for unit in _idle_units_for_move(state, player, move.origin, used_units)[: move.count]:
            used_units.add(unit.id)
        budget -= move_command_cost(state, player, move.target) * move.count

    return SlotPlan(n, train, upgrades, origins, targets, counts)


def _slot_grid(
    slot_lists: list[list[Slot]],
    num_slots: int,
    width: int,
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Stack per-item slot lists into (mask [B,S,C], idx [B,S], valid [B,S])."""
    b = len(slot_lists)
    mask = torch.zeros(b, num_slots, width, dtype=torch.bool)
    idx = torch.zeros(b, num_slots, dtype=torch.long)
    valid = torch.zeros(b, num_slots, dtype=torch.float32)
    for i, slots in enumerate(slot_lists):
        for s, (m, choice) in enumerate(slots[:num_slots]):
            mask[i, s, : len(m)] = torch.tensor(m, dtype=torch.bool)
            idx[i, s] = choice
            valid[i, s] = 1.0
    return mask, idx, valid


_GRID_KEYS = ("train", "upgrade", "origin", "target", "count")


@dataclass
class PreparedBatch:
    features: dict[str, torch.Tensor]  # concatenated graph + batch_index/local_index/num_graphs
    n_max: int
    grids: dict[str, tuple[torch.Tensor, torch.Tensor, torch.Tensor]]  # eval slots per _GRID_KEYS
    im_grids: dict[str, tuple[torch.Tensor, torch.Tensor, torch.Tensor]]  # imitation slots
    im_present: torch.Tensor  # [B] 1.0 where the item has >= 1 imitation term

    @property
    def size(self) -> int:
        return int(self.features["num_graphs"])


@dataclass
class BatchEvaluation:
    logp: torch.Tensor  # [B]
    entropy: torch.Tensor  # [B]
    value: torch.Tensor  # [B]
    imitation_per_item: torch.Tensor  # [B]; mean CE over the item's terms, 0 where absent
    imitation_present: torch.Tensor  # [B]


def prepare_evaluation_batch(
    items: list[tuple[GameState, Side, ActionComponents, ActionComponents | None]],
    device: torch.device,
) -> PreparedBatch:
    """Hoist all model-free per-transition work: observations, masks, indices."""
    cpu = torch.device("cpu")
    xs, edge_attrs, edge_indexes, globals_, batch_ids, local_ids = [], [], [], [], [], []
    plans: list[SlotPlan] = []
    im_plans: list[SlotPlan] = []
    offset = 0
    for b, (state, player, comp, manual_comp) in enumerate(items):
        feats = state_to_tensors(state, player, cpu)
        n = feats["x"].size(0)
        xs.append(feats["x"])
        edge_attrs.append(feats["edge_attr"])
        edge_indexes.append(feats["edge_index"] + offset)
        globals_.append(feats["global_feat"])
        batch_ids.append(torch.full((n,), b, dtype=torch.long))
        local_ids.append(torch.arange(n, dtype=torch.long))
        offset += n
        plans.append(component_slot_plan(state, player, comp))
        if manual_comp is None:
            im_plans.append(SlotPlan(state.map.N, [], [], [], [], []))
        else:
            im_plans.append(imitation_slot_plan(state, player, manual_comp))

    n_max = max(plan.n for plan in plans)
    features = {
        "x": torch.cat(xs),
        "edge_index": torch.cat(edge_indexes, dim=1),
        "edge_attr": torch.cat(edge_attrs),
        "global_feat": torch.stack(globals_),
        "batch_index": torch.cat(batch_ids),
        "local_index": torch.cat(local_ids),
        "num_graphs": torch.tensor(len(items)),
    }

    def grids_for(plan_list: list[SlotPlan]) -> dict[str, tuple[torch.Tensor, torch.Tensor, torch.Tensor]]:
        return {
            "train": _slot_grid([p.train for p in plan_list], 1, 4),
            "upgrade": _slot_grid([p.upgrades for p in plan_list], MAX_UPGRADES_PER_TURN, 1 + n_max),
            "origin": _slot_grid([p.origins for p in plan_list], MAX_MOVE_GROUPS, 1 + n_max),
            "target": _slot_grid([p.targets for p in plan_list], MAX_MOVE_GROUPS, n_max),
            "count": _slot_grid([p.counts for p in plan_list], MAX_MOVE_GROUPS, MAX_MOVE_COUNT + 1),
        }

    grids = grids_for(plans)
    im_grids = grids_for(im_plans)
    im_present = torch.tensor([1.0 if p.num_terms > 0 else 0.0 for p in im_plans], dtype=torch.float32)

    features = {k: v.to(device) for k, v in features.items()}
    grids = {k: tuple(t.to(device) for t in g) for k, g in grids.items()}
    im_grids = {k: tuple(t.to(device) for t in g) for k, g in im_grids.items()}
    return PreparedBatch(features, n_max, grids, im_grids, im_present.to(device))


def _padded_node_scores(vals: torch.Tensor, batch_index: torch.Tensor, local_index: torch.Tensor, num_graphs: int, n_max: int) -> torch.Tensor:
    padded = vals.new_zeros(num_graphs, n_max)
    return padded.index_put((batch_index, local_index), vals)


def _grid_logp_entropy(
    logits: torch.Tensor,  # [B, S, C] (may be expanded views)
    grid: tuple[torch.Tensor, torch.Tensor, torch.Tensor],
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    mask, idx, valid = grid
    masked = logits.masked_fill(~mask, MASK_NEG)
    log_probs = masked - torch.logsumexp(masked, dim=-1, keepdim=True)
    chosen = log_probs.gather(-1, idx.unsqueeze(-1)).squeeze(-1)  # [B, S]
    entropy = -(log_probs.exp() * log_probs).sum(-1)  # [B, S]
    return (chosen * valid).sum(-1), (entropy * valid).sum(-1), chosen


def _slot_logits(out: dict[str, torch.Tensor], features: dict[str, torch.Tensor], n_max: int) -> dict[str, torch.Tensor]:
    batch_index = features["batch_index"]
    local_index = features["local_index"]
    num_graphs = int(features["num_graphs"])
    upgrade_nodes = _padded_node_scores(out["upgrade_logits"], batch_index, local_index, num_graphs, n_max)
    origin_nodes = _padded_node_scores(out["move_origin_logits"], batch_index, local_index, num_graphs, n_max)
    target_nodes = _padded_node_scores(out["move_target_logits"], batch_index, local_index, num_graphs, n_max)
    return {
        "train": out["train_logits"].unsqueeze(1),
        "upgrade": torch.cat([out["upgrade_noop"].unsqueeze(-1), upgrade_nodes], dim=-1)
        .unsqueeze(1)
        .expand(-1, MAX_UPGRADES_PER_TURN, -1),
        "origin": torch.cat([out["move_origin_noop"].unsqueeze(-1), origin_nodes], dim=-1)
        .unsqueeze(1)
        .expand(-1, MAX_MOVE_GROUPS, -1),
        "target": target_nodes.unsqueeze(1).expand(-1, MAX_MOVE_GROUPS, -1),
        "count": out["move_count_logits"].unsqueeze(1).expand(-1, MAX_MOVE_GROUPS, -1),
    }


def evaluate_prepared_batch(model: NextNationGNNPolicy, batch: PreparedBatch) -> BatchEvaluation:
    out = model.forward_batched(batch.features)
    logits = _slot_logits(out, batch.features, batch.n_max)

    logp = None
    entropy = None
    ce_sum = None
    ce_count = None
    for key in _GRID_KEYS:
        slot_logp, slot_entropy, _ = _grid_logp_entropy(logits[key], batch.grids[key])
        logp = slot_logp if logp is None else logp + slot_logp
        entropy = slot_entropy if entropy is None else entropy + slot_entropy

        im_mask, im_idx, im_valid = batch.im_grids[key]
        _, _, im_chosen = _grid_logp_entropy(logits[key], (im_mask, im_idx, im_valid))
        slot_ce = (-(im_chosen) * im_valid).sum(-1)
        slot_count = im_valid.sum(-1)
        ce_sum = slot_ce if ce_sum is None else ce_sum + slot_ce
        ce_count = slot_count if ce_count is None else ce_count + slot_count

    imitation = ce_sum / ce_count.clamp_min(1.0)
    return BatchEvaluation(logp, entropy, out["value"], imitation * batch.im_present, batch.im_present)

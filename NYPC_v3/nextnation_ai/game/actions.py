"""Structured action objects and command conversion helpers."""

from __future__ import annotations

from dataclasses import dataclass, field

from .map import absolute_node
from .state import GameState, Side, UnitId


@dataclass(frozen=True)
class MoveCommand:
    unit_id: UnitId
    target: int


@dataclass(frozen=True)
class MoveGroupAction:
    origin: int
    target: int | None
    count: int


@dataclass
class CommandAction:
    train_n: int = 0
    upgrades: list[int] = field(default_factory=list)
    moves: list[MoveCommand] = field(default_factory=list)


@dataclass
class NNAction:
    train_n: int = 0
    upgrades: list[int] = field(default_factory=list)
    moves: list[MoveGroupAction] = field(default_factory=list)
    old_logp: float | None = None
    old_value: float | None = None
    entropy: float | None = None


def decode_move_group(
    state: GameState,
    player: Side,
    action: MoveGroupAction,
    used: set[UnitId] | None = None,
) -> list[MoveCommand]:
    """Decode one group into MOVE commands.

    ``used`` tracks units already consumed by earlier groups this turn, so two
    groups with the same origin never select the same unit (which would be a
    duplicate MOVE and an instant loss). The set is updated in place.
    """
    if action.target is None or action.count <= 0:
        return []
    units = state.get_idle_units_at(action.origin, player)
    if used is not None:
        units = [unit for unit in units if unit.id not in used]
    selected = units[: action.count]
    if used is not None:
        used.update(unit.id for unit in selected)
    return [MoveCommand(unit.id, action.target) for unit in selected]


def decode_nn_action(
    state: GameState,
    player: Side,
    action: NNAction,
    *,
    canonical_nodes: bool = False,
) -> CommandAction:
    n = state.map.N
    upgrades = [
        absolute_node(region, player, n) if canonical_nodes else region
        for region in action.upgrades
    ]
    moves: list[MoveCommand] = []
    used: set[UnitId] = set()
    for group in action.moves:
        origin = absolute_node(group.origin, player, n) if canonical_nodes else group.origin
        target = None if group.target is None else (
            absolute_node(group.target, player, n) if canonical_nodes else group.target
        )
        moves.extend(decode_move_group(state, player, MoveGroupAction(origin, target, group.count), used))
    return CommandAction(action.train_n, upgrades, moves)


def ensure_command_action(state: GameState, player: Side, action: CommandAction | NNAction) -> CommandAction:
    if isinstance(action, CommandAction):
        return action
    return decode_nn_action(state, player, action)


def action_to_commands(action: CommandAction, *, include_train_zero: bool = True) -> list[str]:
    lines = ["COMMAND"]
    for region in action.upgrades:
        lines.append(f"UPGRADE {region}")
    for move in action.moves:
        lines.append(f"MOVE {move.unit_id} {move.target}")
    if include_train_zero or action.train_n > 0:
        lines.append(f"TRAIN {action.train_n}")
    lines.append("END")
    return lines


def empty_action() -> CommandAction:
    return CommandAction()

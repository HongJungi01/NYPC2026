"""Legal action masks and validation."""

from __future__ import annotations

from .actions import CommandAction, MoveCommand, NNAction, ensure_command_action
from .constants import BASE_BUILD_COST, BASE_TABLE, MOVE_COST, TRAIN_COST
from .state import Building, BuildingKind, GameState, Side


class ActionValidationError(ValueError):
    def __init__(self, player: Side, message: str):
        super().__init__(message)
        self.player = player
        self.message = message


def _upgradeable_regions(state: GameState) -> set[int]:
    return {0, state.map.N - 1, *state.map.points}


def legal_train_mask(state: GameState, player: Side) -> list[bool]:
    mask = [False, False, False, False]
    hq = state.get_hq(player)
    if hq is None:
        mask[0] = True
        return mask
    gold = state.player(player).gold
    for n in range(hq.train_cap + 1):
        if TRAIN_COST * n <= gold:
            mask[n] = True
    return mask


def upgrade_cost(state: GameState, player: Side, region: int) -> int | None:
    if region < 0 or region >= state.map.N:
        return None
    if region not in _upgradeable_regions(state):
        return None
    building = state.buildings.get(region)
    if building is None:
        if region == 0 or region == state.map.N - 1:
            return None
        return BASE_BUILD_COST
    if building.owner is not player:
        return None
    return building.repair_cost if building.level >= building.max_level else building.next_level_cost


def can_upgrade_at(
    state: GameState,
    player: Side,
    region: int,
    *,
    available_gold: int | None = None,
    already_selected: set[int] | None = None,
) -> bool:
    if already_selected and region in already_selected:
        return False
    cost = upgrade_cost(state, player, region)
    if cost is None:
        return False
    if available_gold is None:
        available_gold = state.player(player).gold
    if cost > available_gold:
        return False
    if state.has_alive_enemy_at(region, player):
        return False
    if not state.has_alive_friendly_at(region, player):
        return False
    return True


def legal_upgrade_mask_and_cost(
    state: GameState,
    player: Side,
    *,
    reserved_gold: int = 0,
    already_selected: set[int] | None = None,
) -> tuple[list[bool], list[int | None]]:
    available_gold = state.player(player).gold - reserved_gold
    mask = [False] * state.map.N
    costs: list[int | None] = [None] * state.map.N
    for region in range(state.map.N):
        cost = upgrade_cost(state, player, region)
        costs[region] = cost
        mask[region] = can_upgrade_at(
            state,
            player,
            region,
            available_gold=available_gold,
            already_selected=already_selected,
        )
    return mask, costs


def legal_move_origins(state: GameState, player: Side) -> list[int]:
    origins = {
        unit.pos
        for unit in state.units_for(player)
        if unit.target is None
    }
    return sorted(origins)


def move_command_cost(state: GameState, player: Side, target: int) -> int:
    building = state.buildings.get(target)
    return 0 if building is not None and building.owner is player else MOVE_COST


def legal_move_target_mask(
    state: GameState,
    player: Side,
    origin: int,
    *,
    budget: int | None = None,
) -> list[bool]:
    if budget is None:
        budget = state.player(player).gold
    has_idle = bool(state.get_idle_units_at(origin, player))
    mask = [False] * state.map.N
    if not has_idle:
        return mask
    for target in range(state.map.N):
        if state.map.is_reachable(origin, target) and move_command_cost(state, player, target) <= budget:
            mask[target] = True
    return mask


def legal_move_count_mask(
    state: GameState,
    player: Side,
    origin: int,
    target: int | None,
    *,
    budget: int | None = None,
) -> list[bool]:
    units = state.get_idle_units_at(origin, player)
    mask = [False] * (len(units) + 1)
    mask[0] = True
    if target is None or target < 0 or target >= state.map.N or not state.map.is_reachable(origin, target):
        return mask
    if budget is None:
        budget = state.player(player).gold
    cost = move_command_cost(state, player, target)
    max_count = len(units) if cost == 0 else min(len(units), budget // cost)
    for count in range(1, max_count + 1):
        mask[count] = True
    return mask


def _validate_upgrade_on_temp(state: GameState, player: Side, region: int) -> None:
    if region < 0 or region >= state.map.N:
        raise ActionValidationError(player, f"UPGRADE region out of range: {region}")
    if region not in _upgradeable_regions(state):
        raise ActionValidationError(player, f"UPGRADE target is not a stronghold or HQ: {region}")
    if state.has_alive_enemy_at(region, player):
        raise ActionValidationError(player, f"UPGRADE enemy warrior present at {region}")
    if not state.has_alive_friendly_at(region, player):
        raise ActionValidationError(player, f"UPGRADE without friendly warrior at {region}")

    pstate = state.player(player)
    building = state.buildings.get(region)
    if building is None:
        if region == 0 or region == state.map.N - 1:
            raise ActionValidationError(player, f"UPGRADE cannot build on HQ region {region}")
        if pstate.gold < BASE_BUILD_COST:
            raise ActionValidationError(player, "UPGRADE build insufficient gold")
        pstate.gold -= BASE_BUILD_COST
        state.buildings[region] = Building(player, BuildingKind.BASE, 1, BASE_TABLE[1].max_hp)
        return

    if building.owner is not player:
        raise ActionValidationError(player, f"UPGRADE enemy-owned building at {region}")
    cost = building.repair_cost if building.level >= building.max_level else building.next_level_cost
    if cost is None:
        raise ActionValidationError(player, f"UPGRADE has no available cost at {region}")
    if pstate.gold < cost:
        raise ActionValidationError(player, "UPGRADE insufficient gold")
    pstate.gold -= cost
    building.upgrade_or_repair()


def _validate_move_on_temp(state: GameState, player: Side, move: MoveCommand) -> None:
    if move.unit_id.owner is not player:
        raise ActionValidationError(player, f"MOVE wrong-side unit: {move.unit_id}")
    unit = state.units.get(move.unit_id)
    if unit is None:
        raise ActionValidationError(player, f"MOVE unknown unit: {move.unit_id}")
    if not unit.alive:
        raise ActionValidationError(player, f"MOVE dead unit: {move.unit_id}")
    if unit.target is not None:
        raise ActionValidationError(player, f"MOVE unit already moving: {move.unit_id}")
    if move.target < 0 or move.target >= state.map.N:
        raise ActionValidationError(player, f"MOVE target out of range: {move.target}")
    if not state.map.is_reachable(unit.pos, move.target):
        raise ActionValidationError(player, f"MOVE unreachable target: {move.target}")
    cost = move_command_cost(state, player, move.target)
    pstate = state.player(player)
    if pstate.gold < cost:
        raise ActionValidationError(player, "MOVE insufficient gold")
    pstate.gold -= cost
    unit.target = move.target


def _validate_train_on_temp(state: GameState, player: Side, train_n: int) -> None:
    if train_n < 0:
        raise ActionValidationError(player, "TRAIN n must be non-negative")
    if train_n == 0:
        return
    hq = state.get_hq(player)
    if hq is None:
        raise ActionValidationError(player, "TRAIN after HQ destroyed")
    if train_n > hq.train_cap:
        raise ActionValidationError(player, "TRAIN exceeds HQ train cap")
    cost = train_n * TRAIN_COST
    pstate = state.player(player)
    if pstate.gold < cost:
        raise ActionValidationError(player, "TRAIN insufficient gold")
    pstate.gold -= cost


def validate_or_raise(state: GameState, player: Side, action: CommandAction | NNAction) -> None:
    temp = state.clone()
    command = ensure_command_action(temp, player, action)

    seen_upgrades: set[int] = set()
    for region in command.upgrades:
        if region in seen_upgrades:
            raise ActionValidationError(player, f"duplicate UPGRADE for region {region}")
        seen_upgrades.add(region)
        _validate_upgrade_on_temp(temp, player, region)

    seen_units: set[object] = set()
    for move in command.moves:
        if move.unit_id in seen_units:
            raise ActionValidationError(player, f"duplicate MOVE for unit {move.unit_id}")
        seen_units.add(move.unit_id)
        _validate_move_on_temp(temp, player, move)

    _validate_train_on_temp(temp, player, command.train_n)


def validate_action(state: GameState, player: Side, action: CommandAction | NNAction) -> bool:
    try:
        validate_or_raise(state, player, action)
    except ActionValidationError:
        return False
    return True

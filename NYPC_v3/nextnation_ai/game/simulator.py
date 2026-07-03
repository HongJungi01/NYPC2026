"""Exact local simulator for NEXT NATION daily resolution."""

from __future__ import annotations

from dataclasses import dataclass, field

from .actions import CommandAction, NNAction, ensure_command_action
from .constants import (
    BASE_BUILD_COST,
    BASE_TABLE,
    FOOD_COST,
    HQ_TABLE,
    INCOME_PER_WORKER,
    MAX_DAYS,
    TRAIN_COST,
)
from .legal import ActionValidationError, _upgradeable_regions, move_command_cost
from .state import Building, BuildingKind, GameState, Side, Unit, UnitId


@dataclass
class DayEvents:
    upgrades: list[tuple[int, Side]] = field(default_factory=list)
    trained: list[UnitId] = field(default_factory=list)
    moves: list[tuple[UnitId, int]] = field(default_factory=list)
    damages: list[tuple[str, UnitId, int]] = field(default_factory=list)
    sieges: list[tuple[Side, int, int]] = field(default_factory=list)


@dataclass
class StepResult:
    state: GameState
    events: DayEvents
    invalid_sides: list[Side] = field(default_factory=list)

    @property
    def done(self) -> bool:
        return self.state.done

    @property
    def winner(self) -> Side | None:
        return self.state.winner


class Simulator:
    def step(
        self,
        state: GameState,
        action_left: CommandAction | NNAction,
        action_right: CommandAction | NNAction,
    ) -> StepResult:
        st = state.clone()
        st.day = state.day + 1
        events = DayEvents()

        left = ensure_command_action(st, Side.LEFT, action_left)
        right = ensure_command_action(st, Side.RIGHT, action_right)

        invalid: list[Side] = []
        pending_train: dict[Side, int] = {Side.LEFT: 0, Side.RIGHT: 0}
        for side, action in ((Side.LEFT, left), (Side.RIGHT, right)):
            try:
                self.apply_upgrades(st, side, action, events)
                self.apply_moves(st, side, action)
                pending_train[side] = self.apply_training_charge(st, side, action)
            except ActionValidationError:
                invalid.append(side)

        if invalid:
            st.done = True
            st.terminal_reason = "WA"
            if len(invalid) == 1:
                st.winner = invalid[0].opponent
            else:
                st.winner = None
            return StepResult(st, events, invalid)

        self.apply_day_movement(st, events)
        self.spawn_trained(st, Side.LEFT, pending_train[Side.LEFT], events)
        self.spawn_trained(st, Side.RIGHT, pending_train[Side.RIGHT], events)
        siege_damage = self.apply_day_combat(st, events)
        self.apply_day_siege(st, siege_damage, events)
        self.apply_evening_work(st)
        self.apply_evening_supply(st, events)
        self.check_terminal(st)
        return StepResult(st, events)

    def apply_upgrades(
        self,
        state: GameState,
        player: Side,
        action: CommandAction,
        events: DayEvents,
    ) -> None:
        seen: set[int] = set()
        for region in action.upgrades:
            if region in seen:
                raise ActionValidationError(player, f"duplicate UPGRADE for region {region}")
            seen.add(region)
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
            else:
                if building.owner is not player:
                    raise ActionValidationError(player, f"UPGRADE enemy-owned building at {region}")
                cost = building.repair_cost if building.level >= building.max_level else building.next_level_cost
                if cost is None:
                    raise ActionValidationError(player, f"UPGRADE unavailable at {region}")
                if pstate.gold < cost:
                    raise ActionValidationError(player, "UPGRADE insufficient gold")
                pstate.gold -= cost
                building.upgrade_or_repair()
            events.upgrades.append((region, player))

    def apply_moves(self, state: GameState, player: Side, action: CommandAction) -> None:
        seen: set[UnitId] = set()
        for move in action.moves:
            if move.unit_id in seen:
                raise ActionValidationError(player, f"duplicate MOVE for unit {move.unit_id}")
            seen.add(move.unit_id)
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

    def apply_training_charge(self, state: GameState, player: Side, action: CommandAction) -> int:
        n = action.train_n
        if n < 0:
            raise ActionValidationError(player, "TRAIN n must be non-negative")
        if n == 0:
            return 0
        hq = state.get_hq(player)
        if hq is None:
            raise ActionValidationError(player, "TRAIN after HQ destroyed")
        if n > hq.train_cap:
            raise ActionValidationError(player, "TRAIN exceeds HQ train cap")
        cost = n * TRAIN_COST
        pstate = state.player(player)
        if pstate.gold < cost:
            raise ActionValidationError(player, "TRAIN insufficient gold")
        pstate.gold -= cost
        return n

    def apply_day_movement(self, state: GameState, events: DayEvents) -> None:
        enemy_regions = {
            Side.LEFT: {u.pos for u in state.units.values() if u.owner is Side.RIGHT and u.alive},
            Side.RIGHT: {u.pos for u in state.units.values() if u.owner is Side.LEFT and u.alive},
        }
        for unit_id in sorted(state.units):
            unit = state.units[unit_id]
            if not unit.alive or unit.target is None:
                continue
            target = unit.target
            if unit.pos == target:
                unit.target = None
                continue
            if unit.pos in enemy_regions[unit.owner]:
                continue
            nxt = state.map.next_step(unit.pos, target)
            if nxt is None:
                continue
            unit.pos = nxt
            events.moves.append((unit.id, nxt))
            if unit.pos == target:
                unit.target = None

    def spawn_trained(self, state: GameState, player: Side, count: int, events: DayEvents) -> None:
        if count <= 0:
            return
        hq = state.get_hq(player)
        if hq is None:
            raise ActionValidationError(player, "TRAIN after HQ destroyed")
        pstate = state.player(player)
        for _ in range(count):
            uid = UnitId(player, pstate.next_unit_id)
            pstate.next_unit_id += 1
            hp = HQ_TABLE[hq.level].warrior_hp
            unit = Unit(uid, player, pstate.hq_pos, hp, hp)
            state.units[uid] = unit
            events.trained.append(uid)


    def _damage_tick(self, state: GameState, region: int, target_side: Side) -> UnitId | None:
        candidates = [
            unit
            for unit in state.units.values()
            if unit.pos == region and unit.owner is target_side and unit.hp > 0
        ]
        if not candidates:
            return None
        candidates.sort(key=lambda unit: (unit.hp, unit.id.number))
        victim = candidates[0]
        victim.hp -= 1
        return victim.id

    def apply_day_combat(self, state: GameState, events: DayEvents) -> dict[int, int]:
        counts: dict[int, list[int]] = {}
        for unit in state.units.values():
            if unit.hp > 0:
                counts.setdefault(unit.pos, [0, 0])[unit.owner.value] += 1
        for region in state.buildings:
            counts.setdefault(region, [0, 0])

        damage_accum: dict[tuple[str, UnitId], int] = {}
        siege_damage: dict[int, int] = {}

        for region in sorted(counts):
            left_count, right_count = counts[region]
            building = state.buildings.get(region)
            left_building = building is not None and building.owner is Side.LEFT
            right_building = building is not None and building.owner is Side.RIGHT
            turret = building.turret if building is not None else 0

            left_present = left_count > 0 or left_building
            right_present = right_count > 0 or right_building
            if not (left_present and right_present):
                continue

            left_attacks = left_count + (turret if left_building else 0)
            right_attacks = right_count + (turret if right_building else 0)

            left_idle = 0
            for i in range(left_attacks):
                cause = "TURRET" if left_building and i < turret else "COMBAT"
                victim = self._damage_tick(state, region, Side.RIGHT)
                if victim is None:
                    left_idle += 1
                else:
                    damage_accum[(cause, victim)] = damage_accum.get((cause, victim), 0) + 1

            right_idle = 0
            for i in range(right_attacks):
                cause = "TURRET" if right_building and i < turret else "COMBAT"
                victim = self._damage_tick(state, region, Side.LEFT)
                if victim is None:
                    right_idle += 1
                else:
                    damage_accum[(cause, victim)] = damage_accum.get((cause, victim), 0) + 1

            if building is not None:
                attacker_idle = right_idle if building.owner is Side.LEFT else left_idle
                if attacker_idle > 0:
                    siege_damage[region] = attacker_idle

        for (cause, unit_id), damage in damage_accum.items():
            events.damages.append((cause, unit_id, damage))
        return siege_damage

    def apply_day_siege(self, state: GameState, siege_damage: dict[int, int], events: DayEvents) -> None:
        destroyed: list[int] = []
        for region in sorted(state.buildings):
            damage = siege_damage.get(region, 0)
            if damage <= 0:
                continue
            building = state.buildings[region]
            dealt = min(damage, building.hp)
            building.hp -= dealt
            events.sieges.append((building.owner, region, dealt))
            if building.hp <= 0:
                destroyed.append(region)
        for region in destroyed:
            del state.buildings[region]

    def apply_evening_work(self, state: GameState) -> None:
        consumed: set[UnitId] = set()
        for region in sorted(state.buildings):
            building = state.buildings[region]
            eligible = [
                unit
                for unit in state.units.values()
                if unit.owner is building.owner and unit.pos == region and unit.hp > 0 and unit.id not in consumed
            ]
            eligible.sort(key=lambda unit: unit.id.number)
            for unit in eligible[: building.worker_cap]:
                consumed.add(unit.id)
                state.player(building.owner).gold += INCOME_PER_WORKER

    def apply_evening_supply(self, state: GameState, events: DayEvents) -> None:
        for side in (Side.LEFT, Side.RIGHT):
            for unit in state.units_for(side):
                pstate = state.player(side)
                if pstate.gold >= FOOD_COST:
                    pstate.gold -= FOOD_COST
                else:
                    unit.hp -= 1
                    events.damages.append(("HUNGER", unit.id, 1))
        dead = [uid for uid, unit in state.units.items() if unit.hp <= 0]
        for uid in dead:
            del state.units[uid]

    def check_terminal(self, state: GameState) -> None:
        left_hq = state.get_hq(Side.LEFT)
        right_hq = state.get_hq(Side.RIGHT)
        left_alive = left_hq is not None
        right_alive = right_hq is not None
        if left_alive and right_alive and state.day < MAX_DAYS:
            return

        left_hp = left_hq.hp if left_hq is not None else 0
        right_hp = right_hq.hp if right_hq is not None else 0
        left_score = (left_alive, left_hp)
        right_score = (right_alive, right_hp)
        if left_score > right_score:
            state.winner = Side.LEFT
        elif left_score < right_score:
            state.winner = Side.RIGHT
        else:
            state.winner = None
        state.done = True
        state.terminal_reason = "TURN_LIMIT" if left_alive and right_alive else "HQ_DESTROYED"

"""Adapters for manual models stored under manual_model/."""

from __future__ import annotations

import importlib.util
import sys
from dataclasses import dataclass
from pathlib import Path
from types import ModuleType

from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.legal import validate_action
from nextnation_ai.game.state import BuildingKind, GameState, Side, UnitId


_ROOT = Path(__file__).resolve().parents[2]
_DEFAULT_LTH = _ROOT / "manual_model" / "lth-v4-4.py"


def _load_module(path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location("manual_lth_v4_4", path)
    if spec is None or spec.loader is None:
        raise ImportError(f"cannot load manual model: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


@dataclass
class ManualPythonAgent:
    """Run the Python manual bot's Brain.decide against a full local state.

    The original file is an interactive contest bot. This adapter rebuilds its
    data structures from our authoritative simulator state on every decision,
    calls Brain.decide(), and sanitizes the result through our validator.
    """

    path: Path = _DEFAULT_LTH
    name: str = "lth-v4-4"

    def __post_init__(self) -> None:
        self.path = Path(self.path)
        self.module = _load_module(self.path)
        self._path_cache: dict[tuple, object] = {}

    def _side(self, side: Side):
        return self.module.Side.LEFT if side is Side.LEFT else self.module.Side.RIGHT

    def _kind(self, kind: BuildingKind):
        return self.module.BType.HQ if kind is BuildingKind.HQ else self.module.BType.BASE

    def _map_key(self, state: GameState, player: Side) -> tuple:
        m = state.map
        return (
            id(m),
            player.value,
            m.N,
            tuple(m.x),
            tuple(m.y),
            tuple(m.points),
            tuple(tuple(row) for row in m.adj),
        )

    def _manual_map_and_paths(self, state: GameState, player: Side):
        key = self._map_key(state, player)
        cached = self._path_cache.get(key)
        if cached is not None:
            return cached
        m0 = state.map
        mm = self.module.GameMap()
        mm.N = m0.N
        mm.K = m0.K
        mm.x = list(m0.x)
        mm.y = list(m0.y)
        mm.sites = list(m0.points)
        mm.site_set = set(m0.points)
        mm.adj = [list(row) for row in m0.adj]
        mm.me = self._side(player)
        mm.my_hq = state.player(player).hq_pos
        mm.enemy_hq = state.player(player.opponent).hq_pos
        paths = self.module.Paths(mm)
        self._path_cache[key] = (mm, paths)
        return mm, paths

    def _manual_state(self, state: GameState, player: Side):
        ms = self.module.GameState()
        ms.gold = state.player(player).gold
        ms.enemy_gold_est = state.player(player.opponent).gold
        ms.my_countdown = state.player(player).remaining_time_tokens
        ms.enemy_countdown = state.player(player.opponent).remaining_time_tokens
        ms.warriors = {}
        for unit in state.units.values():
            side = self._side(unit.owner)
            wid = str(unit.id)
            ms.warriors[wid] = self.module.Warrior(
                wid=wid,
                side=side,
                suffix=unit.id.number,
                region=unit.pos,
                hp=unit.hp,
                moving=unit.target is not None,
                target=unit.target,
            )
        ms.buildings = {}
        for region, building in state.buildings.items():
            ms.buildings[region] = self.module.Building(
                region=region,
                side=self._side(building.owner),
                type=self._kind(building.kind),
                level=building.level,
                hp=building.hp,
            )
        return ms

    def _sanitize(self, state: GameState, player: Side, action: CommandAction) -> CommandAction:
        clean = CommandAction()
        for region in action.upgrades:
            proposal = CommandAction(clean.train_n, [*clean.upgrades, region], list(clean.moves))
            if validate_action(state, player, proposal):
                clean = proposal
        for move in action.moves:
            proposal = CommandAction(clean.train_n, list(clean.upgrades), [*clean.moves, move])
            if validate_action(state, player, proposal):
                clean = proposal
        if action.train_n > 0:
            proposal = CommandAction(action.train_n, list(clean.upgrades), list(clean.moves))
            if validate_action(state, player, proposal):
                clean = proposal
        return clean

    def act(self, state: GameState, player: Side) -> CommandAction:
        mm, paths = self._manual_map_and_paths(state, player)
        ms = self._manual_state(state, player)
        brain = self.module.Brain(mm, ms, paths)
        try:
            raw = brain.decide(max(1, state.day + 1))
        except Exception:
            return CommandAction()
        moves: list[MoveCommand] = []
        for wid, target in raw.moves:
            try:
                uid = UnitId.parse(wid)
            except Exception:
                continue
            if uid.owner is player:
                moves.append(MoveCommand(uid, target))
        action = CommandAction(train_n=max(0, int(raw.train_n)), upgrades=list(raw.upgrades), moves=moves)
        if validate_action(state, player, action):
            return action
        return self._sanitize(state, player, action)

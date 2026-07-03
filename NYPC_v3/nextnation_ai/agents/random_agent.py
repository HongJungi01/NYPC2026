"""Random legal agent used to shake down simulator and validator bugs."""

from __future__ import annotations

import random
from dataclasses import dataclass

from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.legal import legal_move_origins, legal_train_mask, validate_action
from nextnation_ai.game.state import GameState, Side


@dataclass
class RandomLegalAgent:
    seed: int | None = None
    max_upgrades_per_turn: int = 1
    max_move_origins_per_turn: int = 3
    upgrade_probability: float = 0.25
    move_probability: float = 0.35

    def __post_init__(self) -> None:
        self.rng = random.Random(self.seed)

    def act(self, state: GameState, player: Side) -> CommandAction:
        train_options = [i for i, ok in enumerate(legal_train_mask(state, player)) if ok]
        action = CommandAction(train_n=self.rng.choice(train_options or [0]))
        if not validate_action(state, player, action):
            action = CommandAction()

        upgrade_candidates = list(range(state.map.N))
        self.rng.shuffle(upgrade_candidates)
        for region in upgrade_candidates:
            if len(action.upgrades) >= self.max_upgrades_per_turn:
                break
            if self.rng.random() > self.upgrade_probability:
                continue
            proposal = CommandAction(action.train_n, [*action.upgrades, region], list(action.moves))
            if validate_action(state, player, proposal):
                action = proposal

        origins = legal_move_origins(state, player)
        self.rng.shuffle(origins)
        moved_units = {move.unit_id for move in action.moves}
        for origin in origins[: self.max_move_origins_per_turn]:
            if self.rng.random() > self.move_probability:
                continue
            units = [u for u in state.get_idle_units_at(origin, player) if u.id not in moved_units]
            if not units:
                continue
            targets = list({0, state.map.N - 1, *state.map.points, *state.buildings.keys()})
            if not targets:
                targets = list(range(state.map.N))
            self.rng.shuffle(targets)
            for target in targets:
                count = self.rng.randint(1, len(units))
                selected = units[:count]
                proposal = CommandAction(
                    action.train_n,
                    list(action.upgrades),
                    [*action.moves, *[MoveCommand(u.id, target) for u in selected]],
                )
                if validate_action(state, player, proposal):
                    action = proposal
                    moved_units.update(u.id for u in selected)
                    break

        return action if validate_action(state, player, action) else CommandAction()

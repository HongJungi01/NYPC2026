"""Self-play environment wrapper."""

from __future__ import annotations

from dataclasses import dataclass

from .actions import CommandAction
from .legal import validate_or_raise
from .map import GameMap, make_line_map
from .observation import Observation, build_observation
from .reward import shaped_reward
from .simulator import Simulator
from .state import Side, create_initial_state


@dataclass
class EnvStep:
    obs_left: Observation
    obs_right: Observation
    reward_left: float
    reward_right: float
    done: bool
    info: dict


class NextNationEnv:
    def __init__(self, game_map: GameMap | None = None, shaping_beta: float = 0.20):
        self.game_map = game_map or make_line_map()
        self.shaping_beta = shaping_beta
        self.simulator = Simulator()
        self.state = create_initial_state(self.game_map)

    def reset(self, game_map: GameMap | None = None) -> tuple[Observation, Observation]:
        if game_map is not None:
            self.game_map = game_map
        self.state = create_initial_state(self.game_map)
        return build_observation(self.state, Side.LEFT), build_observation(self.state, Side.RIGHT)

    def step(self, action_left: CommandAction, action_right: CommandAction) -> EnvStep:
        validate_or_raise(self.state, Side.LEFT, action_left)
        validate_or_raise(self.state, Side.RIGHT, action_right)
        before = self.state
        result = self.simulator.step(before, action_left, action_right)
        self.state = result.state
        reward_left = shaped_reward(before, self.state, Side.LEFT, self.shaping_beta, action_left)
        reward_right = shaped_reward(before, self.state, Side.RIGHT, self.shaping_beta, action_right)
        return EnvStep(
            build_observation(self.state, Side.LEFT),
            build_observation(self.state, Side.RIGHT),
            reward_left,
            reward_right,
            self.state.done,
            {
                "winner": self.state.winner,
                "reason": self.state.terminal_reason,
                "events": result.events,
            },
        )

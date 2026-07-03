"""DEPRECATED neural policy wrapper.

Built on ``nn.action_dist``, which returns random legal actions regardless of
the model. For a real neural agent, call
``nextnation_ai.nn.torch_policy.select_action`` directly (see
``scripts/train_torch.py::opponent_act`` for the pattern).
"""

from __future__ import annotations

from dataclasses import dataclass

from nextnation_ai.game.actions import CommandAction
from nextnation_ai.game.observation import build_observation
from nextnation_ai.game.state import GameState, Side
from nextnation_ai.nn.action_dist import sample_action


@dataclass
class NeuralAgent:
    model: object
    deterministic: bool = False

    def act(self, state: GameState, player: Side) -> CommandAction:
        obs = build_observation(state, player)
        return sample_action(self.model, obs, state, player, deterministic=self.deterministic).action

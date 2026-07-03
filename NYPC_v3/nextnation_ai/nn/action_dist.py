"""DEPRECATED legacy placeholder — not a working neural sampler.

``sample_action`` ignores the model's policy heads and returns
``RandomLegalAgent`` actions (only the value estimate is read). The real
hierarchical policy with masked sampling and PPO-consistent evaluation lives
in ``nextnation_ai.nn.torch_policy``. Do not wire this into experiments.
"""

from __future__ import annotations

import warnings
from dataclasses import dataclass

warnings.warn(
    "nextnation_ai.nn.action_dist is a legacy placeholder: sample_action() "
    "returns random legal actions and ignores the model's policy heads. "
    "Use nextnation_ai.nn.torch_policy instead.",
    DeprecationWarning,
    stacklevel=2,
)

from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game.actions import CommandAction
from nextnation_ai.game.observation import Observation, make_pyg_data
from nextnation_ai.game.state import GameState, Side


@dataclass
class SampledAction:
    action: CommandAction
    logp: float
    value: float
    entropy: float


def sample_action(
    model,
    obs: Observation,
    state: GameState,
    player: Side,
    *,
    deterministic: bool = False,
) -> SampledAction:
    """Sample a legal action.

    The current implementation deliberately falls back to the random legal
    policy after model inference. That keeps illegal commands impossible while
    the project-specific hierarchical policy heads are being trained.
    """
    value = 0.0
    try:
        data = make_pyg_data(obs)
        out = model(data)
        raw_value = out.get("value")
        if raw_value is not None:
            value = float(raw_value.detach().cpu().reshape(-1)[0])
    except Exception:
        value = 0.0
    action = RandomLegalAgent(seed=None).act(state, player)
    return SampledAction(action=action, logp=0.0, value=value, entropy=0.0)


def evaluate_action(model, obs: Observation, action: CommandAction, state: GameState, player: Side) -> SampledAction:
    sampled = sample_action(model, obs, state, player, deterministic=True)
    return SampledAction(action=action, logp=sampled.logp, value=sampled.value, entropy=sampled.entropy)

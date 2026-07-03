"""Self-play rollout collection."""

from __future__ import annotations

from dataclasses import dataclass

from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game.env import NextNationEnv
from nextnation_ai.game.state import Side
from nextnation_ai.rl.rollout_buffer import RolloutBuffer, RolloutItem


@dataclass
class SelfPlayRunner:
    env: NextNationEnv
    left_agent: object
    right_agent: object | None = None

    def collect_rollout(self, rollout_len: int = 64) -> RolloutBuffer:
        buffer = RolloutBuffer()
        self.env.reset()
        right_agent = self.right_agent or self.left_agent
        for _ in range(rollout_len):
            before_obs_left = self.env.state.clone()
            action_left = self.left_agent.act(self.env.state, Side.LEFT)
            action_right = right_agent.act(self.env.state, Side.RIGHT)
            step = self.env.step(action_left, action_right)
            buffer.add(RolloutItem(before_obs_left, action_left, step.reward_left, step.done, 0.0, 0.0))
            if step.done:
                self.env.reset()
        return buffer


def random_self_play_runner(env: NextNationEnv | None = None) -> SelfPlayRunner:
    return SelfPlayRunner(env or NextNationEnv(), RandomLegalAgent(), RandomLegalAgent())

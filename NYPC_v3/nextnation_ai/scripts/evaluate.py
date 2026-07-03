#!/usr/bin/env python3
"""Evaluate legal agents in the local simulator."""

from __future__ import annotations

from nextnation_ai.agents.random_agent import RandomLegalAgent
from nextnation_ai.game.env import NextNationEnv
from nextnation_ai.game.state import Side


def main() -> None:
    env = NextNationEnv()
    left = RandomLegalAgent(seed=11)
    right = RandomLegalAgent(seed=22)
    env.reset()
    while not env.state.done:
        step = env.step(left.act(env.state, Side.LEFT), right.act(env.state, Side.RIGHT))
    print({"winner": env.state.winner, "reason": env.state.terminal_reason, "day": env.state.day})


if __name__ == "__main__":
    main()

"""Generalized Advantage Estimation."""

from __future__ import annotations


def compute_gae(
    rewards: list[float],
    values: list[float],
    dones: list[bool],
    *,
    gamma: float = 0.997,
    gae_lambda: float = 0.95,
    last_value: float = 0.0,
) -> tuple[list[float], list[float]]:
    if len(values) != len(rewards):
        raise ValueError("values and rewards must have the same length")
    advantages = [0.0] * len(rewards)
    gae = 0.0
    next_value = last_value
    for t in reversed(range(len(rewards))):
        not_done = 0.0 if dones[t] else 1.0
        delta = rewards[t] + gamma * next_value * not_done - values[t]
        gae = delta + gamma * gae_lambda * not_done * gae
        advantages[t] = gae
        next_value = values[t]
    returns = [adv + value for adv, value in zip(advantages, values)]
    return returns, advantages

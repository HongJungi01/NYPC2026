"""Compatibility exports for the plan's Step 9 model.py."""

from __future__ import annotations

from .actor_critic import build_actor_critic
from .gnn_encoder import build_encoder


def GraphEncoder(node_dim: int, edge_dim: int, hidden_dim: int = 128, layers: int = 4):
    return build_encoder(node_dim, edge_dim, hidden_dim, layers)


def NextNationActorCritic(
    node_dim: int,
    edge_dim: int,
    global_dim: int,
    hidden_dim: int = 128,
    layers: int = 4,
):
    return build_actor_critic(node_dim, edge_dim, global_dim, hidden_dim, layers)


__all__ = ["GraphEncoder", "NextNationActorCritic", "build_actor_critic", "build_encoder"]

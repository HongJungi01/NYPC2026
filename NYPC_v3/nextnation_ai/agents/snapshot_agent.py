"""DEPRECATED frozen model snapshot agent (see neural_agent.py — the
underlying sampler is a random-legal placeholder, not the trained policy)."""

from __future__ import annotations

import copy
from dataclasses import dataclass

from .neural_agent import NeuralAgent


@dataclass
class SnapshotAgent(NeuralAgent):
    @classmethod
    def from_model(cls, model: object, deterministic: bool = True) -> "SnapshotAgent":
        snapshot = copy.deepcopy(model)
        if hasattr(snapshot, "eval"):
            snapshot.eval()
        return cls(snapshot, deterministic=deterministic)

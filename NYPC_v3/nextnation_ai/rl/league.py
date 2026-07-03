"""Snapshot league for self-play opponents."""

from __future__ import annotations

import copy
import random
from dataclasses import dataclass, field


@dataclass
class League:
    snapshots: list[object] = field(default_factory=list)
    rng: random.Random = field(default_factory=random.Random)

    def add_snapshot(self, model: object) -> None:
        snapshot = copy.deepcopy(model)
        if hasattr(snapshot, "eval"):
            snapshot.eval()
        self.snapshots.append(snapshot)

    def sample(self) -> object | None:
        if not self.snapshots:
            return None
        return self.rng.choice(self.snapshots)

"""Rollout storage for PPO."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any


@dataclass
class RolloutItem:
    obs: Any
    action: Any
    reward: float
    done: bool
    old_logp: float
    old_value: float
    return_: float | None = None
    advantage: float | None = None


@dataclass
class RolloutBuffer:
    items: list[RolloutItem] = field(default_factory=list)

    def add(self, item: RolloutItem) -> None:
        self.items.append(item)

    def clear(self) -> None:
        self.items.clear()

    def __len__(self) -> int:
        return len(self.items)

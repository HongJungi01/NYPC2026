"""Compatibility exports for the plan's self_play.py step."""

from __future__ import annotations

from .self_play_runner import SelfPlayRunner, random_self_play_runner

__all__ = ["SelfPlayRunner", "random_self_play_runner"]

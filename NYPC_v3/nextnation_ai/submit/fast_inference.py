"""Fast command formatting for submission-time agents."""

from __future__ import annotations

from nextnation_ai.game.actions import CommandAction, action_to_commands


def format_command(action: CommandAction) -> str:
    return "\n".join(action_to_commands(action)) + "\n"

#!/usr/bin/env python3
"""Model export placeholder — NOT a working exporter.

Trained GNN checkpoints are saved by scripts/train_torch.py under
runs/gnn_ppo/checkpoints/ and consumed directly by submit/torch_policy_bot.py
(--checkpoint); no separate export step exists yet.
"""

from __future__ import annotations


def main() -> None:
    raise SystemExit("No trained model checkpoint has been configured for export yet.")


if __name__ == "__main__":
    main()

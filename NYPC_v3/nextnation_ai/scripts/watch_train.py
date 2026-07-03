#!/usr/bin/env python3
"""Phase-criteria dashboard for a train_torch run (BC_SPEC.md follow-up).

Usage: python -m nextnation_ai.scripts.watch_train <run_dir> [--window 100]

Prints the rolling values that drive the teacher-intervention decisions:
  - gate state + rolling sampled win rate vs the manual pool
  - eval win rate vs the teacher / vs lth-v4-4 / pool-wide eval_score
  - imitation loss, entropy, approx_kl (collapse / forgetting indicators)
Phase triggers (see final report):
  P2 anneal   : handled automatically by --manual-gate-winrate
  P3 wean     : eval winrate vs teacher >= 0.70 over the last 3 evals AND
                eval_score >= 0.0
  intervene   : imitation > 1.2 with flat eval, or entropy < 0.8 with
                clip_fraction < 0.01
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--window", type=int, default=100, help="Updates in the rolling train window.")
    args = parser.parse_args()

    rows = []
    with (args.run_dir / "train_log.jsonl").open(encoding="utf-8") as f:
        for line in f:
            try:
                d = json.loads(line)
            except json.JSONDecodeError:
                continue
            if d.get("type") == "update":
                rows.append(d)
    if not rows:
        print("no updates logged yet")
        return

    win = rows[-args.window:]
    last = rows[-1]
    t = last["train"]
    print(f"update {last['update']} | beta {last.get('shaping_beta'):.3f} | bc_coef {last.get('manual_bc_coef'):.3f} "
          f"| manual_share {last.get('manual_weight'):.2f}")
    print(f"gate_passed {last.get('gate_passed')} (opened at update {last.get('gate_update')}) "
          f"| rolling sampled winrate vs manual pool: {last.get('manual_winrate_window')}")
    print(f"train: entropy {t['entropy']:.3f} | imitation {t['imitation_loss']:.3f} | "
          f"approx_kl {t['approx_kl']:.4f} | clip_fraction {t['clip_fraction']:.3f} | avg_return {t['avg_return']:+.3f}")

    evals = [r for r in rows if r.get("eval")]
    if evals:
        last3 = evals[-3:]
        def rate(name_prefix: str) -> str:
            g = w = 0
            for r in last3:
                for name, rec in r["eval"].items():
                    if name.startswith(name_prefix):
                        g += rec["games"]
                        w += rec["wins"]
            return f"{w}/{g} ({w / g:.2f})" if g else "n/a"

        print(f"eval (last {len(last3)} evals): score {evals[-1]['eval_score']:+.3f} | "
              f"vs teacher {rate('manual:hjk-07022029f')} | vs lth-v4-4 {rate('manual:lth-v4-4')} | "
              f"vs pool {rate('manual:')}")

    # self-play activity: HQ kills end games early; 200-day games are stalemates
    self_days = [
        rec["avg_days"]
        for r in win
        for name, rec in r["train_opponents"].items()
        if name == "self"
    ]
    if self_days:
        print(f"self-play avg days over window: {sum(self_days) / len(self_days):.0f} "
              f"(200 = passive stalemates, <150 = decisive games)")


if __name__ == "__main__":
    main()

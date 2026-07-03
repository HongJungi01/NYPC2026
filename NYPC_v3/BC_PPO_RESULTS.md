# BC + PPO execution results (2026-07-03)

Executed the phased plan from BC_SPEC.md end to end and evaluated against the
criteria. **Headline: the BC bootstrap fixed the passivity/rush collapse (the
model now plays coherent economy), but PPO on top is counterproductive, and the
resulting neural policy is substantially weaker than the C++ search bots — it
beats none of them.** This is a strategic result that needs a decision before
investing further.

## What was run

| stage | artifact | outcome |
|---|---|---|
| Dataset (deterministic bots ⇒ map-diversity is the only axis) | `runs/bc_data/games_big.jsonl` (3,150 games, 150 maps) | 0 failures, replay-verified |
| BC pretrain (30 epochs, 429k items) | `runs/bc_pretrain_big/checkpoints/best.pt` | held-out CE 0.579; P(upgrade\|teacher-upgrade-day)=0.82; sampled play builds ~4–5 bases/game |
| PPO run 1 (full anchor, gate) | `runs/gnn_bc_200` (stopped @250) | flat eval ≈ −0.85; gate never opened; **degraded greedy** |
| PPO run 2 (beatable curriculum, annealed anchor) | `runs/gnn_bc_200b` (stopped @340) | oscillated 0.00–0.33 beatable-winrate, no trend; **worse than BC** |

## Key findings (measured, unseen maps)

1. **Greedy (argmax) decoding is broken**: 0/42 vs the pool — it loses to
   *random*. Independent per-head argmax of the hierarchical policy produces
   incoherent joint actions. Sampled decoding is strictly better.
2. **PPO degrades the BC policy.** The 340-update checkpoint scores 0/6 vs the
   hard-3 (lth/jsh/teacher) in both sampled and best-of-8, versus BC's 1/6.
   Run 1 (constant anchor) merely preserved BC-vs-weak; run 2 (annealed anchor)
   drifted the policy off the good BC distribution and got worse. Adversarial
   sparse reward vs strong opponents pushes the policy away from BC without
   finding anything better.
3. **The base policy is the ceiling.** BC + sampled wins ≈ 0.05–0.19 vs the
   pool (high map variance), driven almost entirely by wins vs random and the
   occasional lth-v4-4. **0 wins vs any of the 6 hjk C++ bots or the teacher**,
   in every decoding tried.
4. **Search doesn't rescue it.** Best-of-N (sample N, score by 1-ply potential)
   gives a small, unreliable lift; K-day rollout decoding is *worse* because it
   rolls out with the broken greedy policy. Search built on a weak base policy
   stays weak.

## What actually improved

Relative to the prior `gnn_scratch_*` runs (which collapsed to noop / turn-1
rush and did nothing), the BC model plays **real, coherent economy** — saves
gold, builds bases, staffs workers, trains armies, and beats random/weak bots.
That collapse is fixed. But it did **not** produce a higher win rate vs the
strong search bots than the old turtle policy did.

## Recommendation (decision needed)

- **Best available artifact:** `runs/bc_pretrain_big/checkpoints/best.pt`, played
  with **sampled** (not greedy) decoding. Use this over any `gnn_scratch_*` or
  `gnn_bc_*` PPO checkpoint.
- **Do not run more PPO as configured** — it degrades the model.
- **To actually beat the C++ search bots**, a reactive policy net is the wrong
  tool; the realistic paths are:
  1. **AlphaZero-style**: train policy+value via MCTS self-play on the exact
     simulator (we have it, judge-verified). Largest effort, best ceiling.
  2. **Much heavier RL** with a redesigned dense reward that gives gradient
     between "lose fast" and "lose slow" vs strong opponents. Uncertain payoff.
  3. **Accept the search bots** already in `manual_model/` as the stronger
     competitor and ship one of those instead of the net.

These differ by 10×+ in effort, so the next step is a call on how much to invest.

## Addendum — outcome-only league self-play (tested the win/draw/loss idea)

Tested the proposal to drop shaping and train on pure win/draw/loss with league
self-play (self + past-self snapshots + manual pool). Results:

- **It is the best-performing configuration** — outcome-only self-play reached
  the best in-training evals of any run and, unlike shaped PPO, briefly moved
  the *greedy* eval. Directionally the right idea.
- **But the improvement is not real/stable.** Two decisive problems surfaced:
  1. **The trainer eval uses only 2 fixed maps** (`maps[:2]`), so `eval_score`
     swings ±0.4 between adjacent evals on a near-identical policy and rewards
     map-specific quirks. The "peak" checkpoint (oo @150, in-training −0.373
     with 1/4 vs each strong bot) scores **greedy 0/42, sampled 5/42 (beats
     only random)** on an independent 3-map probe. The apparent trend was
     largely fixed-map variance.
  2. **Greedy (argmax) decoding is on a knife-edge.** The policy flips between an
     "active mode" (beats random/jsh, steals 1 game vs a strong bot on a
     specific map) and a "turtle mode", decided by tiny weight changes — so
     eval_score is not a smoothly optimizable signal, and resuming from the
     peak with stabilized hyperparameters (lower entropy bonus, higher anchor)
     regressed rather than climbed.

**Net across all four runs:** no neural checkpoint beats *any* C++ search bot on
held-out maps; the ceiling is "beats random". The greedy decode is broken and
the 2-map eval was misleading me toward false positives.

## Bottom line and recommendation

1. **BC fixed the collapse** (model plays real economy, beats random) — a real
   qualitative improvement over the `gnn_scratch_*` baseline. But **policy-
   gradient RL on top does not reach C++-search-bot strength**, in any of the
   four configs tried.
2. **Two concrete, cheap fixes regardless of direction:** (a) inference must use
   **sampled or search decoding, never argmax** (greedy is 0/42); (b) evaluate
   on **many maps** (≥8), not 2 — the 2-map eval produced false signal.
3. **To get a neural bot that beats the search bots, the principled path is
   MCTS/AlphaZero**: search at inference fixes the broken argmax *and* gives
   stable value targets that fix the noisy-signal training problem. This is a
   substantial build. Alternatively, **ship a C++ search bot** — note the
   teacher (hjk-07022029f) is *not* the strongest (it loses to hjk-07010244 /
   07020507 / 07020517 ~15–29%); a round-robin among those would pick the best.

## Reusable tooling added

- `scripts/collect_bc_games.py`, `scripts/pretrain_bc.py` — dataset + BC.
- `scripts/train_torch.py --manual-train-include` — curriculum subset for
  training while eval keeps the full pool.
- `scripts/watch_train.py` — phase/health dashboard for any run.

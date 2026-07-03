# BC_SPEC — Teacher-bootstrap plan for the GNN policy (2026-07-02)

Diagnosis (from `runs/gnn_scratch_120` + `runs/gnn_scratch_200` logs): the policy
collapsed into a passive no-op optimum — 0 upgrades in all recent eval games,
~95% empty days, 100% self-play draws for 1,500+ updates, eval score flat.
Root cause is state distribution, not labels: on the model's own gold-starved
states even the teacher's correct action is "do nothing", so on-policy DAgger
labels (and self-play) reinforce passivity. Empty teacher labels are **correct
and must stay in the loss** (budget exhausted ⇒ nothing to do); the fix is to
train on **teacher-visited states**, where saving → base → workers → army is
demonstrated.

Teacher: `manual_model/hjk-07022029f.cpp` (StrategyManager bot, referee-exact
internal simulator). It is interactive/stateful, so it cannot label arbitrary
states; it teaches through its own trajectories instead.

## 1. `scripts/collect_bc_games.py` (new)

- Compile the teacher with `g++ -O2 -std=c++17`.
- Play full games (to the real terminal, `MAX_DAYS=200`) of teacher vs:
  mirror (teacher), every other compiled `manual_model/*.cpp` bot, `lth-v4-4`,
  `RandomLegalAgent`, and an **idle** (always-empty) opponent — the idle games
  demonstrate how to punish exactly the turtle the current policy plays.
  Both sides, several maps (`generate_map`, NP=25 KP=4, seeded), default ≈400
  games, ProcessPoolExecutor parallelism.
- `ManualCppAgent` with generous timeouts (10 s) so the teacher plays at full
  strength during collection (training-time opponents keep the 0.5 s default).
- Record one JSONL line per game: map rows, per-day **as-simulated** (post-
  sanitize) `CommandAction`s of both sides, teacher side(s), winner/reason/days.
- Integrity: after recording, replay through `Simulator` and assert the final
  state signature matches (the simulator is deterministic and judge-exact, so
  replay reconstructs every intermediate state).

## 2. `scripts/pretrain_bc.py` (new)

- Replay each game; for every teacher-side day emit an item
  `(state_before, side, teacher ActionComponents)` — **including empty
  actions** (`imitation_slot_plan` already encodes them as explicit
  train-0/no-upgrade/no-move targets, and they stay in the CE).
- Value-head init: regress `value` to the discounted return of
  `shaped_reward` (new reward.py, γ=0.997, β=`--shaping-beta` default 0.18)
  for the teacher side; games always reach a real terminal so no bootstrap.
- Loss = mean imitation CE + `--value-coef`·MSE − `--entropy-coef`·entropy
  (small entropy bonus, default 0.005, keeps heads soft for the PPO handoff).
- 10% of games held out for validation CE; per-epoch rollout probes (greedy
  policy vs random and vs idle) reporting upgrades/game, non-empty days, wins.
- `ENEMY_GOLD_NOISE` on, to match PPO-time observations.
- Saves `train_torch`-compatible checkpoints (`--init-checkpoint` ready);
  best = lowest validation CE.

## 3. `game/reward.py`

- Terminal draw: 0.0 → **−0.3 for both sides** (mutual passivity must not be a
  comfortable fixed point of self-play; a draw ranks below any win).
- Φ economy re-weight (still antisymmetric, potential-based, non-farmable):
  hq_hp 0.35→**0.30**, income 0.12→**0.25**, building_value 0.08→**0.15**,
  unit_hp/map_control/frontline/hq_threat stay 0.05.
- `tests/test_reward.py` updated (draw expectation, weights).

## 4. `scripts/train_torch.py`

- `--bc-dataset PATH` (+ `--bc-pool-items`, default 8192, `--bc-chunks-per-update`,
  default 2): replayed dataset items are prepared once into fixed evaluation
  chunks; every `ppo_update` samples chunks and adds their CE to the imitation
  term (weighted by the existing `manual_bc_coef` schedule). BC chunks
  contribute **only** the imitation term — no policy/value/entropy gradient.
- On-policy DAgger labels (`manual_components` from lth-v4-4) become opt-in:
  `--dagger-labels` (default off). Off also removes a large per-step rollout
  cost.
- Competence-gated annealing: `--manual-gate-winrate` (default 0.5),
  `--manual-gate-window` (default 100): the opponent-mix / BC-coef / beta
  schedules hold at their start values until the rolling win rate vs manual
  opponents reaches the threshold, then anneal over the remaining updates.
- Teacher joins the opponent pool automatically (`manual_model/*.cpp` glob).

## 5. Recommended relaunch

```
.venv/bin/python -m nextnation_ai.scripts.train_torch \
  --run-dir runs/gnn_bc_200 --updates 2000 --episodes-per-update 16 \
  --max-days 200 --eval-days 200 --init-checkpoint runs/bc_pretrain/checkpoints/best.pt \
  --bc-dataset runs/bc_data/games.jsonl \
  --manual-bc-start 0.20 --manual-bc-end 0.05 \
  --shaping-beta-start 0.18 --shaping-beta-end 0.18 \
  --horizon-score-coef 0 --rollout-workers 16
```

(`--horizon-score-coef 0` ⇒ truncation uses the critic bootstrap; with
`--max-days 200` games end at the real terminal anyway. β annealing to ~0.05
is for a later fine-tune phase.) Do **not** resume `gnn_scratch_*` checkpoints.

## 6. Acceptance

1. Dataset: ≥300 games recorded, replay-integrity check passes on every game.
2. BC policy probe: vs random — win ≥ 9/10, ≥2 upgrades/game, ≥50% non-empty
   days; vs idle — ≥1 upgrade/game and a win.
3. Full permanent test suite passes (incl. judge differential + updated
   reward tests).
4. PPO smoke from the BC checkpoint (~30 updates): no crashes, upgrades still
   present in eval games, self-play no longer 100% zero-reward draws,
   imitation term live.

## Notes / risks

- The currently running tmux training keeps its already-spawned workers, so
  editing these files won't corrupt it silently — but per the analysis it is
  converged into the no-op optimum and should be stopped; its checkpoints
  should not be reused.
- Value head init from teacher-vs-weaker games is optimistic against stronger
  fields; acceptable as an initialization.
- Teacher per-turn latency is unknown until compiled; collection is
  parallelized and its wall time is reported by the collector.

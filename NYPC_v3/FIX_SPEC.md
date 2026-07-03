# NEXT NATION RL — Required Modifications Specification

Date: 2026-07-02
Scope: `nextnation_ai/` (Python RL stack). Based on a full review of PLAN.md, the game
rules (`NEXT NATION.md`), and a differential test of the simulator against the official
judge (`nation-providing/testing-tool.py`).

**Verified correct — do NOT change:**
`game/simulator.py`, `game/map.py`, `game/state.py`, `game/legal.py`, `game/actions.py`
(CommandAction path) match the judge exactly. Differential test: 12 random 200-day games
on generated maps + 1 rush game ending in HQ destruction, full state (gold, unit
pos/hp/target, buildings, winner) compared after every day — all matched. The
sample/evaluate log-prob paths in `nn/torch_policy.py` also matched exactly over a
40-step probe. Any change to these files must re-pass the differential test
(scratchpad `diff_test.py`; re-create from §7 if lost).

Priorities: **P0** = fix before the next training run. **P1** = fix before the next
submission build. **P2** = hygiene / robustness.

---

## 1. [P0] `game/reward.py` — rebuild shaping as pure potential-based reward

### Problem (measured, not theoretical)

Playing the strong manual bot (lth-v4-4) vs a random agent on a generated map:
the manual bot **won** (200 days, HQ-hp tiebreak) but its cumulative shaped return was
**+0.074**, decomposed as:

| component        | cumulative |
|------------------|-----------:|
| terminal (win)   | +1.000 |
| beta·ΔΦ (β=0.24) | +0.085 |
| hq_dmg terms     | +0.000 |
| unit_hp_swing    | +0.016 |
| control_delta    | +0.018 |
| **activity**     | **−1.526** |
| new_base         | +0.480 |

Root causes:

1. `_action_activity_reward` (reward.py:131) is not potential-based and dominates the
   terminal reward. The −0.01 "idle" penalty fires on every turn where the bot
   correctly saves gold for a 600–3,600 HQ/base upgrade — the known-good strategy looks
   like idling. The per-command bonuses (+0.003 train / +0.006 upgrade / +0.0015 move)
   sum to up to +3.0 over 200 days (> |terminal|), and moving units between own
   buildings costs 0 gold, so command-spam is a free reward-farming channel.
2. Only `potential_delta` is multiplied by `beta` (reward.py:180-189). The hq-damage,
   unit-swing, control-delta, activity, and new-base terms are constant, so the plan's
   Phase 3/4 "anneal beta → pure win/loss fine-tuning" (PLAN §11, §23.4) is impossible.
3. Double counting / broken invariance: HQ hp already has weight 0.35 inside Φ; the
   extra `0.25·enemy_hq_damage − 0.30·own_hq_damage` terms re-count it with `max(0,·)`
   clipping, which destroys the telescoping property of potential-based shaping
   (damage-then-repair nets negative). `unit_hp_swing` and `control_delta` likewise
   re-count Φ terms.
4. `_new_base_reward` is a one-directional event bonus that re-counts what Φ's
   income/building-value terms already capture.

### Required change

Replace `shaped_reward` with the plan's form:

```python
GAMMA = 0.997  # keep in sync with the trainer's gamma

def shaped_reward(before, after, player, beta=0.20, action=None):
    return (
        terminal_reward(after, player)
        + beta * (GAMMA * potential_score(after, player) - potential_score(before, player))
    )
```

- **Delete** `_action_activity_reward`, `_new_base_reward`, `_has_nonempty_legal_action`
  (and the `NEW_BASE_REWARD` / `EARLY_BASE_REWARD` constants), and the four extra delta
  terms (`enemy_hq_damage`, `own_hq_damage`, `unit_hp_swing`, `control_delta`) from
  `shaped_reward`.
- If a do-nothing-collapse guard is still wanted, it must be (a) tiny (≤ 0.002),
  (b) conditioned on `_has_nonempty_legal_action(before, player)` — in which case keep
  that helper, and (c) **multiplied by beta** so it anneals away. Prefer relying on the
  entropy bonus + manual-BC imitation loss already present in `scripts/train_torch.py`;
  they solve the same problem without biasing the optimum.

### Φ weight adjustments (`potential_score`)

Keep Φ strictly antisymmetric (it already is). Set:

| term                | current | required |
|---------------------|--------:|---------:|
| `_hq_hp_ratio`      | 0.35 | 0.35 (keep) |
| `_income_norm`      | 0.32 | **0.12** (plan says 0.10; 0.32 skews the whole signal toward economy) |
| `_building_value_norm` | 0.10 | 0.08 |
| `_unit_hp_norm`     | 0.07 | 0.05 |
| `_map_control_norm` | 0.10 | 0.05 |
| `_frontline_norm`   | 0.08 | 0.05 (keep the term — good addition) |
| `_hq_threat_norm`   | 0.09 | 0.05 (keep the term — good addition) |

New-base incentives are covered by `_income_norm` + `_building_value_norm`; no event
bonus needed.

### Terminal reward

- Keep win/lose at ±1.0.
- Change draw from **−0.15 to 0.0** (or at most −0.05). A 200-day equal-HP draw from a
  fortress position is a legitimate contest outcome; −0.15 punishes the garrison
  strategy that previously proved essential.

### Acceptance criteria

- Re-run the §1 measurement (manual bot vs random, seed 4242, NP=25 KP=4): the winning
  side's cumulative return must be ≥ +0.9 and the shaping component must telescope to
  `beta·(γ·Φ(s_T)·… ≈ Φ(s_T) − Φ(s_0))` within numerical noise.
- A scripted "spam bot" (issues max moves/upgrades every turn, never attacks) must
  accumulate **less** return than a scripted quiet winner over the same maps.
- Unit tests in `tests/test_economy.py` that reference `_new_base_reward` must be
  updated/removed with it.

---

## 2. [P0] `scripts/train_torch.py` — anneal beta; fix truncation bootstrap

### 2a. Beta annealing

`--shaping-beta` (default 0.24) is constant for the whole run. Anneal it exactly like
`manual_weight` already is (train_torch.py:524-526):

```python
shaping_beta = lerp(args.shaping_beta_start, args.shaping_beta_end, progress)
```

- New args: `--shaping-beta-start` (default 0.20), `--shaping-beta-end` (default 0.03),
  replacing `--shaping-beta`. Pass the per-update value into `play_episode`.
- Log `shaping_beta` in the metrics dict.

### 2b. Truncation bootstrap

`compute_returns_advantages` (train_torch.py:243) starts every trajectory with
`next_value = 0.0`. For an episode truncated at `--max-days` with
`--horizon-score-coef 0`, the final step's `done` is False and its future is valued at
0 → biased advantages.

Required: bootstrap truncated trajectories with the critic.

- In `play_episode`, when the loop exits with `not env.state.done`, compute
  `last_value = float(select_action(model, env.state, side, device, deterministic=True).value)`
  (under `torch.no_grad()`) for each controlled side and attach it to the trajectory
  (e.g. `Transition.bootstrap_value` on the last item, or return alongside the list).
- In `compute_returns_advantages`, initialize `next_value` from that bootstrap value
  (and treat the final step as `not done`) instead of 0.
- The `horizon_score_coef` potential bonus may be kept, but then the forced
  `done = True` + bonus must remain mutually exclusive with the bootstrap (use one
  mechanism, not both; prefer the critic bootstrap and default
  `--horizon-score-coef 0.0`).

---

## 3. [P1] `submit/torch_policy_bot.py` — enemy-gold estimate must subtract spending

### Problem

Training observations (`observation.py:233`) contain the **true** enemy gold. At
contest time the bot's estimate adds enemy income/upkeep in
`apply_evening_gold_estimate` but never subtracts enemy spending, so it grows without
bound → severe train/inference distribution shift on `enemy_gold_estimate_norm`.

### Required change

In `read_turn_result`, deduct observed enemy costs from
`state.player(opponent).gold`:

1. **UPGRADE events** for the enemy side: cost is exactly inferable from the tracked
   building *before* applying the event —
   `BASE_BUILD_COST` if no building; else `repair_cost` if `level >= max_level`; else
   `next_level_cost`. Deduct, then apply `apply_upgrade_event`.
2. **TRAIN events**: deduct `TRAIN_COST` per enemy unit id in the TRAIN section.
3. **MOVE events**: an enemy unit that was stationary last turn and moved this turn
   implies a new MOVE command → deduct `MOVE_COST` (10). This slightly over-deducts
   when the enemy's destination was its own building (free move) — acceptable; note
   below.
4. Clamp the estimate at ≥ 0.

Tracking note: to implement (3), keep a per-enemy-unit "moved last turn / had pending
move" flag: an enemy unit is *pending* from its first observed step until a turn where
it does not appear in MOVE while unblocked — exact inference is impossible (targets are
hidden); the required behavior is only the first-step deduction, flagged
conservatively: deduct 10 the first time a previously-stationary enemy unit moves, then
mark it moving until it stops appearing in MOVE events for one turn.

Additionally (pick one, document which):

- **(a)** During training, replace the true enemy gold in `build_global_features` with
  a noised estimate (e.g. `true_gold · U(0.8, 1.3) + N(0, 100)`, clamped ≥ 0) so the
  policy is robust to estimate error; or
- **(b)** Reconstruct the same estimator inside the training env so train and inference
  see the identical feature.

Option (a) is less code and is sufficient.

### Also required (same file)

`compute_own_charges_and_targets` (torch_policy_bot.py:194): apply the player's own
UPGRADE effects (create/level the building in local state) **before** computing move
costs, mirroring the judge's phase order (construction → movement charge). Today a
build-base-then-move-to-it turn deducts 10 gold the judge doesn't. The drift is
downward-only (safe, no WA risk), but it starves the gold estimate over long games.
Mirror the fix in `submit/policy_bot.py` (same function copied there).

### Acceptance criteria

- Scripted probe: play the bot vs the sample rush bot via the judge; log
  `abs(est_enemy_gold − true_enemy_gold)` per turn (true value obtainable by running
  the judge as a module). Mean absolute error must stay bounded (< ~300 gold) instead
  of growing linearly.
- Own-gold estimate must remain ≤ true own gold every turn (invariant already holds;
  keep it).

---

## 4. [P1] `nn/torch_policy.py` — fallback must not corrupt PPO ratios

`select_action` (torch_policy.py:460-472): when `_build_action_from_components` output
fails `validate_action`, `comp` is replaced with an empty/train-only action but the
returned `logp` still corresponds to the originally sampled components. A later
`evaluate_components(comp)` then computes `new_logp` for a *different* action than
`old_logp` — a silently corrupted importance ratio.

Required (either is acceptable):

- **(a)** After replacing `comp`, recompute `logps`/`entropies` for the replacement via
  `_evaluate_logp_entropy_for_components(out, state, player, comp, device)` and return
  those; or
- **(b)** Mark the `PolicyStep` (e.g. `fallback=True`) and drop such transitions in
  `play_episode` before they enter the buffer.

Also add a counter/log line when the fallback triggers: with correct masks it should be
0; a nonzero rate indicates a mask bug and must fail loudly in CI (assert in tests,
warn in training).

---

## 5. [P2] `game/actions.py` — `decode_nn_action` duplicate-unit trap

`decode_move_group` selects `get_idle_units_at(origin)[:count]` against the unmutated
state, so two `MoveGroupAction`s with the same origin select the same units → duplicate
MOVE → validation failure. The torch pipeline is unaffected (it builds `CommandAction`s
with a `used` set), but the `NNAction` path through `env.step`/`Simulator.step` is a
landmine.

Required: track consumed unit ids across groups in `decode_nn_action`:

```python
used: set[UnitId] = set()
for group in action.moves:
    ...
    units = [u for u in state.get_idle_units_at(origin, player) if u.id not in used]
    selected = units[:group.count]
    used.update(u.id for u in selected)
    moves.extend(MoveCommand(u.id, target) for u in selected)
```

Add a regression test: two groups, same origin, counts 2+2 with 3 idle units → decoded
moves are 3 unique units, action validates.

---

## 6. [P2] Vestigial modules — remove or clearly mark

- `nn/action_dist.py`: `sample_action` ignores the model and returns
  `RandomLegalAgent` actions. `agents/neural_agent.py` and `agents/snapshot_agent.py`
  sit on top of it, so `NeuralAgent(model).act(...)` is a random bot. Either delete
  these three (plus `nn/model.py`, `nn/actor_critic.py`, `nn/gnn_encoder.py` if
  unused), or add a module-level `DeprecationWarning` and a docstring pointing to
  `nn/torch_policy.py` as the real pipeline. No experiment may wire `NeuralAgent` into
  evaluation believing it is neural.
- `scripts/export_model.py` is a placeholder that raises — fine, but list it in the
  README/PLAN status so it isn't mistaken for a working exporter.

---

## 7. [P2] Regression harness — make the differential test permanent

Add `tests/test_judge_differential.py` that imports
`nation-providing/testing-tool.py` as a module and, for 2–3 seeds with small maps
(`NP=25, KP=4`, ~60 days, RandomLegalAgent both sides), applies identical commands to
the judge's phase functions and `Simulator.step`, asserting equal state snapshots
(gold, `{unit: (pos, hp, target)}`, `{region: (owner, kind, level, hp)}`) after every
day, plus one scripted one-sided-rush game asserting the HQ-destruction terminal path
and winner. This is the session's throwaway `diff_test.py` turned into CI; keep runtime
< ~60 s by capping days/seeds.

Also add:

- A torch test asserting `select_action(...).logp == evaluate_components(...).logp`
  (exact) for ~20 sampled steps with a fixed seed — guards the PPO ratio invariant.
- A reward test asserting antisymmetry: `potential_score(s, LEFT) == −potential_score(s, RIGHT)`
  on a few constructed states, and `shaped_reward` telescoping over an episode.

---

## 8. Minor notes (no action required, documented for awareness)

- The judge WAs on an explicit `TRAIN 0` after the HQ is destroyed; the local simulator
  does not. Unreachable in real play (the judge ends the game the day an HQ falls).
- `agents/cpp_manual_agent.py::observe` emits result sections in event order, while the
  judge sorts merged results (upgrades by region; damages by cause/side/suffix). Only
  matters if a C++ manual bot depends on ordering; none currently does.
- `GraphEncoder` in `torch_policy.py` is simpler than PLAN §7 (messages are
  `h[src] + e` through a shared MLP; no `[h_src, h_dst, e]` message MLP, and global
  features enter only at pooling). This is a capability gap, not a bug — acceptable for
  now; revisit if the policy plateaus.
- `nn/simple_policy.py` `_avg_distance_to`/`extract_features` normalize by
  `max(dist)`, which is `INF_DISTANCE` on disconnected maps; generated maps are always
  connected, so this only matters for synthetic test maps.

---

## Suggested execution order

1. §1 reward rewrite + §2a beta annealing (small, same PR, unblocks training).
2. §2b truncation bootstrap.
3. §7 regression harness (locks in the verified-correct simulator before further work).
4. §4 fallback logp fix, §5 decode fix (quick).
5. §3 submit-bot gold estimator (+ optional training-side noise) before the next
   submission build.
6. §6 cleanup whenever convenient.

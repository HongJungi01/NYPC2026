# NEXT NATION Team Strategy Notes (distilled from real play)

> Source: 2026-06-30 team discussion (Lee Taehoon, Hong Junki, Noh Jiwoong, Jang Seongho),
> capturing strategy learned from actual matches. This document keeps **only game-strategy
> content** — banter/meta (neural-net/RL feasibility, team roles, ranking, token/tooling) is
> excluded. Each principle is mapped to the current bot (`submission/hybrid.cpp`) status.
> Approach consensus (scope): NN/RL is unrealistic this week → go **heuristics + exact-simulator search**.

Korean original: [TeamStrategy.ko.md](TeamStrategy.ko.md)

---

## 1. Confirmed game mechanics (cross-checked against code constants)

Hard facts agreed in the discussion that match `submission/hybrid.cpp` constants.

| Fact | Value / source |
|---|---|
| Each newly move-ordered soldier costs **10 gold**, but **0 if the destination is a friendly building** | `MOVE_COST=10` [hybrid.cpp:34](../submission/hybrid.cpp#L34), free branch in `issue_move` [:728](../submission/hybrid.cpp#L728) |
| A kill yields no gold but denies the enemy **120 gold of retraining + future production** | `TRAIN_COST=120` [:35](../submission/hybrid.cpp#L35) |
| A building costs **300 gold**; daily income `15 × min(garrison, work_cap)` | `BASE_BUILD_COST=300`, `WORK_INCOME=15` [:36-37](../submission/hybrid.cpp#L36) |
| Upkeep **2 gold/day per soldier**; at 0 gold soldiers starve and lose hp | `UPKEEP_PER_WARRIOR=2` [:38](../submission/hybrid.cpp#L38) |
| **HQ level↑ → spawned unit HP 4→5→6→7→8**, work_cap 1→5, train_cap 1→3, turret/HQ-HP↑ | `HQ_LEVELS` [:51-54](../submission/hybrid.cpp#L51) |
| Start: 500 gold, 3 soldiers, 200 turns | `START_GOLD/START_WARRIORS/MAX_TURN` [:31-33](../submission/hybrid.cpp#L31) |
| **Perfect-information zero-sum** — no fog. Referee sends both sides' events each turn. **Only gold is hidden** | observation model in [§4](#4-battlefield-observation-model) |

Implications:
- **A single unit is valuable.** Early game produces ~1 unit/day, so losses are slow to recover.
  A trade is only worth it at roughly minimum-force-for-a-building; a plain 1:1 unit trade loses.
- **Rush is mostly non-viable.** While the opponent raises a 300-gold building, pouring units in
  rarely buys a numeric edge because buildings hit back with attack-1. → economy first.
- **The axis of the game = how many bases you take and how fast (= gold income).** The key skill is
  **defending each base with minimum force and attacking with minimum force.**

---

## 2. Strategy principles × code status

✅ implemented · 🟡 partial · 🔴 not yet (backlog → [§3](#3-application-backlog-priority))

| Principle (real-play rationale) | Status | Code location / note |
|---|---|---|
| Don't scatter; **concentrate on buildable land (strongholds) then garrison** | ✅ | nearest-first expansion `build_strat` [:661](../submission/hybrid.cpp#L661), work_cap garrison laborers [:844](../submission/hybrid.cpp#L844) |
| **Number of bases = gold income** decides the game | ✅ | `side_income` [:584](../submission/hybrid.cpp#L584), EconomyManager build/upgrade [:754](../submission/hybrid.cpp#L754) |
| Keep **HQ level ≥ the opponent's** (unit HP, train_cap, defense, tiebreak) | ✅ | HQ-level matching [:769](../submission/hybrid.cpp#L769) |
| **Rush response** = attrit near home + recall garrison + train simultaneously | ✅ | anticipatory defense [:814](../submission/hybrid.cpp#L814), total mobilization [:884](../submission/hybrid.cpp#L884), emergency training [:970](../submission/hybrid.cpp#L970) |
| Perfect info → assume opponent strategy and respond (mirror rollout) | ✅ | `strategy_manager` K-day lookahead [:1051](../submission/hybrid.cpp#L1051) |
| **Minimum-force attack/defense** — commit only what's needed, never dribble | 🟡 | `min_mass_to_take`/`min_defenders_to_hold` [:562](../submission/hybrid.cpp#L562) exist, but attack streams the whole army [:958](../submission/hybrid.cpp#L958) (to avoid tiebreak draws) |
| **Count turns; all-out attack/defense at the strong timing** | 🟡 | K-day rollout picks posture + `attack_turn` gate [:954](../submission/hybrid.cpp#L954); no explicit "strong timing" trigger |
| **Rush non-viable → economy first** | 🟡 | RUSH is still a candidate posture (`presets::RUSH`) [:652](../submission/hybrid.cpp#L652); lookahead may pick it → reconsider weighting/candidate set |
| Losing a unit hurts; **a trade must be building-grade** to pay off | 🔴 | eval's army-hp diff is linear 4.0 [:619](../submission/hybrid.cpp#L619); no explicit trade-quality gate |
| **Center land**: only valuable after ~50–70 turns; point-symmetry means the moment the opponent sinks 300 gold there you gain a force edge to **retake**; it's the **interception choke** for enemy rushes | 🔴 | no center detection / turn-gated value / interception positioning |
| **Early opening**: HQ + 2 nearest lands; the 4th unit takes the 3rd land (mine first if gold-short) | 🔴 | greedy expand only (build when a unit is present and budget≥300) [:933](../submission/hybrid.cpp#L933); no deterministic opening / mine-first sequencing |
| Evaluate max gold income via **alpha-beta pruning** | 🔴 | currently K-day rollout; AB/makespan search is a separate approach tied to [StarCraft.md](StarCraft.md) §5 BOSS |

---

## 3. Application backlog (priority)

> ⚠ Everything below is eval/`gen_cmds` heuristic territory, where **overfitting/regression is the
> norm**. Adopt only after **same-seed paired control** (swap -a/-b on the same map, 2 games).
> A single game's W/L is buried in board variance.

1. **Turn-gated center-land value + interception choke** — promote the center stronghold in
   `expand_order` behind a turn ≥ ~50 gate, and add a center-occupancy/interception term to
   `eval_state`. Validate the point-symmetric retake hypothesis (~2.5-unit edge after the
   opponent's 300-gold investment) in simulation.
2. **Deterministic early opening** — a 3-land opening sequence + "mine first if you can't afford the
   build." Accelerates mid-game economy.
3. **Kill value / trade quality** — weight unit loss by retraining cost (120) in eval, and gate
   unfavorable-trade attacks. (Partially covered by `min_mass` → lower priority.)
4. **Alpha-beta gold optimization** — large effort; defer, bundled with BOSS makespan search.

---

## 4. Battlefield observation model

Each turn the referee sends both sides' event blocks (UPGRADE/TRAIN/MOVE/DAMAGE/SIEGE) and **hides
gold for both sides.** Therefore:

- **Enemy unit positions, per-region counts, buildings and levels = exactly observed** — `init_sim`
  seeds the start, `read_turn_result` accumulates events to reconstruct both sides' `SimState`.
  *This is observation, not estimation.*
- **Only opponent gold is truly hidden** → estimated (start 500 − observed spend + income − upkeep).
  Within that, only **move-cost timing** is approximate (build/train/income/upkeep are exact).
  Tightening this estimate and validating the reconstruction is a separate WorldModel effort (in progress).

---

*This note is a snapshot of the live discussion. When the code changes, update the §2 status column.*

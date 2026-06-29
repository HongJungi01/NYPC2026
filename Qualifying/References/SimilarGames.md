# Similar games & cross-domain AI techniques — for NEXT NATION bots

The companion to [StarCraft.md](StarCraft.md). StarCraft is one source of ideas, but NEXT
NATION ([../Rule.md](../Rule.md)) is — precisely — a **graph-territory, simultaneous-move,
deterministic, perfect-information, ~100ms-turn-budget** game. That profile matches
**programming-game competitions** even better than StarCraft, and the strongest, most directly
reusable ideas (graph ROI, rolling-horizon search, influence maps, assignment matching) come
from there. This doc collects those games, their bots, and the techniques, each with an
algorithmic summary + NEXT NATION mapping.

> Korean reading copy / 한글 읽기판: [SimilarGames.ko.md](SimilarGames.ko.md). Index: [README.md](README.md).
> Cross-domain synthesis that prompted this doc: [GeminiReference.md](GeminiReference.md).

---

## 1. Why look past StarCraft

NEXT NATION's exact traits and the lever each gives:

- **Turn-based + ~100ms budget** → no background thinking; must *decide within the budget*.
  → favors **anytime heuristic search** (RHEA / beam) over real-time reactive control.
- **Deterministic + perfect information** → an **exact forward simulator** is buildable (see
  StarCraft.md §5 keystone). → "simulate candidate plans, score, pick best" beats hand rules.
- **Simultaneous (blind) submission + all units act at once** → branching explodes; classic
  minimax/AB doesn't fit. → favors evolving/searching over **macro-action *sequences*** (RHEA),
  not per-unit move trees.
- **Movement on a region graph** → distances/fronts are graph quantities. → **influence maps**
  and **temporal ROI** on the graph (we already have Floyd-Warshall `dist[][]`).

**Meta-lesson from every competition below:** with a deterministic model and a turn budget, the
winning shape is *forward-simulate a handful of candidate plans and keep the best*, wrapped in a
clean state evaluator — not real-time reaction (StarCraft) nor full game-tree minimax (chess).

---

## 2. Closest analogue games (+ their bots)

### Planet Wars — Google AI Challenge 2010 *(closest mathematical model)*
Graph of planets (≈ our regions/strongholds); each has production; send fleets along edges;
combat is deterministic subtraction by arriving counts. Almost exactly NEXT NATION's
graph-capture + deterministic-combat core.
- [Gabor Melis — winning bot (code)](https://github.com/melisgl/planet-wars) · [benzedrine postmortem](https://www.benzedrine.ch/planetwars.html) · [satirist Planet Wars hub](http://www.satirist.org/ai/planetwars/)
- **NEXT NATION takeaway:** temporal ROI of capturing a node (§3); a separate **reinforcement
  "positioner" pass** that forwards idle units toward the front; route fleets through friendly
  intermediate nodes (free-move analogue).

### Generals.io *(closest living analogue)*
Capture the enemy **general** (= HQ) by expanding territory and producing army each turn on a
grid. HQ-kill win condition + per-turn production map directly. (Caveat: it has fog-of-war and
is ~real-time; NEXT NATION is perfect-info + turn-based.)
- [bot server / API](https://bot.generals.io/) · [EklipZ_ai (strong bot)](https://github.com/EklipZgit/generals-bot) · [harrischristiansen/generals-bot](https://github.com/harrischristiansen/generals-bot) · [A3C policy bot](https://yilundu.github.io/2017/09/05/A3C-and-Policy-Bot-on-Generals.io.html) · [RL paper 2025](https://arxiv.org/html/2507.06825v2) · [strategy notes](https://gist.github.com/twolfson/4bbbb40bd8b7ed670694b5a4dae04931)
- **NEXT NATION takeaway:** task-based bot (expand / attack / defend tasks scored by metrics like
  enemy threat); tempo — capture just before a reinforcement tick; choke blocking & army interception.

### microRTS (Ontañón) *(richest academic search source)*
Minimal 2-player zero-sum RTS testbed built for AI research — the place algorithms are compared
cleanly. Bots: **PuppetSearch** (search over scripts), **NaïveMCTS / Naive Sampling**
(combinatorial multi-unit actions), **StrategyTactics** (strategy search + tactical MCTS), and a
**DRL competition winner**.
- [microRTS AI competition](https://sites.google.com/site/micrortsaicompetition/microrts) · [First competition report (PDF)](https://scispace.com/pdf/the-first-microrts-artificial-intelligence-competition-34aoi2r58w.pdf) · [DRL winner (2024)](https://arxiv.org/html/2402.08112v1)
- **NEXT NATION takeaway:** search over **scripts/abstract actions**, not raw move combos
  (PuppetSearch); Naive Sampling to tame the combinatorial "everyone moves at once" action space.

### Halite II / III (Two Sigma) *(simultaneous resource/territory)*
Build ships from a shipyard, mine a resource, manage dropoffs; simultaneous moves; most-resource
wins. Famous for **Hungarian (Kuhn–Munkres) matching** to assign ships→cells optimally.
- [Halite III repo](https://github.com/HaliteChallenge/Halite-III) · [Wikipedia](https://en.wikipedia.org/wiki/Halite_AI_Programming_Competition) · [example postmortem bot](https://github.com/dzou/halite3)
- **NEXT NATION takeaway:** **assignment matching** (§3) for warrior→{labor slot / expansion
  target / attack front}; prioritize highest-value targets globally rather than greedily per-unit.

### Kore 2022 (Kaggle) *(simultaneous multi-step "plans")*
Build shipyards, mine "kore", launch fleets with **multi-step flight plans** (string programs);
most kore (or eliminate opponent) by turn 400. Top approach: **imitation learning** from the
leaderboard's best.
- [Kore 2022 competition](https://www.kaggle.com/competitions/kore-2022) · [imitation-learning solution](https://github.com/khanhvu207/kore2022)
- **NEXT NATION takeaway:** treat an action as a **multi-day plan** (e.g. "move A→sh, build,
  labor") not a single command; imitation from strong logs is a viable later track.

### Lux AI — Seasons 1–3 (Kaggle / NeurIPS) *(simultaneous build/units at scale)*
Two players gather resources, build units/factories, expand; simultaneous; large unit counts.
Baselines span rule-based, brute-force search, RL, and imitation; billions of frames provided.
- [lux-ai.org](https://www.lux-ai.org/) · [Lux S2 (NeurIPS'23)](https://github.com/Lux-AI-Challenge/Lux-Design-S2) · [Lux S3 (NeurIPS'24)](https://github.com/Lux-AI-Challenge/Lux-Design-S3)
- **NEXT NATION takeaway:** confirms the rule-based/search baseline is competitive before RL;
  good source of multi-agent unit-management patterns.

### Battlecode (MIT) *(RTS programming competition)*
Write an autonomous army controller; deterministic engine; emphasis on pathfinding, distributed
coordination, and a fixed compute budget per turn.
- [battlecode.org](https://battlecode.org/) · [2020 postmortem](https://stonet2000.github.io/battlecode/2020/)
- **NEXT NATION takeaway:** disciplined budget management and robust pathfinding patterns.

### Adjacent (lower priority — different info model)
- **Diplomacy / CICERO** (Meta) — simultaneous-order; planning + (here irrelevant) negotiation: [ai.meta.com/research/cicero](https://ai.meta.com/research/cicero/).
- **Stratego / DeepNash** (DeepMind) — imperfect-info model-free RL: [Science 2022](https://www.science.org/doi/10.1126/science.add4679).
- **Freeciv / Civilization** — 4X turn-based macro (economy/tech), but slower-horizon than ours.

---

## 3. Cross-domain techniques (the payload)

Each: *algorithm in 2–4 lines* + *NEXT NATION fit*. These plug into the StarCraft.md §5 building
blocks (the **exact forward simulator** is the shared keystone for all search-based ones).

### RHEA — Rolling Horizon Evolutionary Algorithm *(primary search candidate)*
*Algorithm:* encode the next **K turns of macro-actions** as a genome; keep a population; mutate
/ recombine; **evaluate each genome by running the forward simulator** to horizon K and scoring
the end state; play the first action of the best genome; **roll the horizon** forward next turn
(seed with the previous best). Beats MCTS in multi-action / continuous / simultaneous games.
- [RHEA for GVGP (ToG 2021, PDF)](http://www.diego-perez.net/papers/RHEAforGVGP_ToG21.pdf) · [arXiv](https://arxiv.org/pdf/2003.12331) · [Gaina PhD thesis](https://rdgain.github.io/assets/pdf/papers/gaina2021phd.pdf)
- **NEXT NATION fit:** genome = per-day `(UPGRADE/ TRAIN n / MOVE list)` for K days; eval = state
  evaluator (StarCraft.md §5); cleanly handles "all warriors move at once" that breaks minimax.

### Beam search + simulated annealing *(CodinGame contest meta)*
*Algorithm:* **beam** = breadth-first but keep only top-K states by eval each ply (depth ~60 ×
width ~1000 is feasible in ms); **SA** = perturb a plan sequence, accept worse moves with
decaying probability to escape local optima. Both wrap the forward simulator.
- [Coders Strike Back report](https://www.codingame.com/blog/coders-strike-back-contest-report/) · [SA primer](https://www.codingame.com/learn/simulated-annealing) · [stochastic search write-up](https://www.codingame.com/blog/stochastic-algorithm-smash-the-code/)
- **NEXT NATION fit:** beam over daily macro-plans (dedupe equal states); SA over a plan's
  move/build/train choices when the plan space is rugged.

### MCTS for multi-unit actions
*Algorithm:* **Naive Sampling / NaïveMCTS** factor the joint multi-unit action so the tree
doesn't explode; **PuppetSearch / script-&-cluster UCT** search over a few **scripts** (or unit
clusters) instead of raw actions.
- microRTS bots (above) · [Script- & cluster-based UCT (Justesen 2014)](http://julian.togelius.com/Justesen2014Script.pdf)
- **NEXT NATION fit:** search over a small menu of scripted postures/build-templates rather than
  the raw command product; an alternative to RHEA for the decision layer.

### Hungarian / assignment matching
*Algorithm:* Kuhn–Munkres finds the max-weight perfect matching between agents and targets in
O(n³). *NEXT NATION fit:* weight = value of warrior *w* doing target *t* (fill labor slot /
claim stronghold / reinforce front); solve once per turn for a globally optimal assignment
instead of greedy per-warrior choices.

### Influence / threat maps
*Algorithm:* spread each unit's strength over the map decaying with distance; per node value =
Σ friendly − Σ enemy; **front line = high (enemy × ally)**; route to maximize own influence,
avoid deep-negative nodes.
- [Game AI Pro 2 — Modular Tactical Influence Maps (Dave Mark)](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter30_Modular_Tactical_Influence_Maps.pdf) · [Spatial Reasoning for Strategic Decision Making (Kevin Dill)](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter31_Spatial_Reasoning_for_Strategic_Decision_Making.pdf)
- **NEXT NATION fit:** compute per-region influence using Floyd-Warshall distances; Movement
  manager only commits where `ally − enemy ≥ margin` (graph version of the anti-dribble rule),
  and picks rally points on the computed front.

### Temporal / ROI node valuation *(Planet Wars)*
*Algorithm:* value of capturing a node now = `production × (turns_left) − travel_cost`, compared
across candidate targets. *NEXT NATION fit:* base ROI = `15·work_cap·(200 − arrival_day) −
(300 build + 10·moves + future upkeep)`; ranks expansion targets and "is this worth it this late?".

### Logistics / safety-margin resource management
*Algorithm:* maintain projected cash-flow ≥ 0 against upkeep; never issue commands that cause a
starvation tick. *NEXT NATION fit (from [GeminiReference.md](GeminiReference.md)):* keep rear
buildings labor-saturated to fund the front; hard-**filter** any plan whose evening upkeep would
exceed gold (the −1hp hunger "suicide"). A safety net regardless of which search picks the plan.

### Imitation / RL *(reference only — infra-heavy, later track)*
Learn a policy from strong logs (Kore, Lux) or self-play league (AlphaStar in StarCraft.md;
Generals A3C). *NEXT NATION fit:* possible once we have a strong scripted/search bot to imitate
or self-play against; not the first build.

---

## 4. Synthesis — recommended direction for NEXT NATION

Refines the [GeminiReference.md](GeminiReference.md) hybrid:

1. **Exact forward simulator** (StarCraft.md §5) — the keystone; build & log-test first.
2. **State evaluator** — `(myHQ_hp − enemyHQ_hp)·BIG + future_gold·w + (#numeric-superiority regions)·w` (Planet-Wars-style), terminal = HQ-kill / day-200 HP tiebreak.
3. **Decision layer = forward-sim heuristic search under the 100ms budget** — **RHEA** (or beam)
   over K-day macro-action plans; optionally script/puppet search for the posture menu.
4. **Spatial layer** — influence map (front, rally, safe vs suicidal regions) + temporal ROI for
   expansion ordering + Hungarian assignment for warrior→task.
5. **Safety filter** — logistics rule vetoes any starving / clearly-losing-combat command.
6. A thin **manager skeleton** (StarCraft.md §4a) organizes the above; the StarCraft managers are
   the *frame*, the search + spatial techniques here are the *brain*.

This is the "manager frame + Planet-Wars/CodinGame-style fast forward-sim search" approach — the
best fit for a deterministic, graph-based, turn-budgeted game.

---

## 5. Relation to the other docs
- [StarCraft.md](StarCraft.md) — StarCraft-specific architectures + the shared building blocks (§5 there).
- **This doc** — the broader-games half: analogue games, their bots, and the cross-domain techniques.
- [GeminiReference.md](GeminiReference.md) — the cross-domain synthesis that motivated this.
- [README.md](README.md) — index / how to use the library.

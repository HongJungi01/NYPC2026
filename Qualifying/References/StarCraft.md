# StarCraft-AI reference library — for NEXT NATION bots

A reusable knowledge base for building **NEXT NATION** bots from proven StarCraft-AI design.
We will try **several architectures** (§4), all sharing the same building blocks (§5). This
doc is written to be both **scannable** (for a human) and **actionable** (each technique has
an algorithmic summary + how it maps to NEXT NATION, so implementation can start from here).

Game rules: [../Rule.md](../Rule.md). Reference referee (authoritative rule resolution):
[../Seongho/files_3/testing-tool-practice.py](../Seongho/files_3/testing-tool-practice.py).

> Korean reading copy / 한글 읽기판: [StarCraft.ko.md](StarCraft.ko.md). Companion doc:
> [SimilarGames.md](SimilarGames.md) (non-StarCraft analogue games & techniques). Index: [README.md](README.md).

---

## 1. How to use this doc

- §2 — why these StarCraft techniques transfer, and the one big lever this game gives us.
- §3 — the reference library (links + what each is + NEXT NATION use).
- §4 — the candidate architectures we plan to build & compare; each cites the refs it needs.
- §5 — the shared building blocks every architecture reuses (build these once, well).

When implementing architecture *X*: read its §4 entry → pull the §5 blocks it lists → open the
§3 sources cited there for detail.

---

## 2. Why StarCraft AI transfers to NEXT NATION

NEXT NATION is a 2-player, **turn-based, deterministic, full-information** RTS on a
**point-symmetric region graph** (N≤109 regions, ≤200 days; strongholds ≈ expandable bases;
HQs at region `0` and `N-1`). Commands are submitted **simultaneously (blind)** and the day
resolves in a fixed order.

**The big lever:** because combat is deterministic and the whole state is observable each turn
(the result block reports both sides' upgrades/trains/moves/damage/sieges), we can build an
**EXACT forward simulator**. The StarCraft community only ever had *approximate* combat sims
(SparCraft); here the simulator can be the referee's rules 1:1 → shallow lookahead is reliable.

| StarCraft AI subsystem | NEXT NATION analogue |
|---|---|
| Manager decomposition (UAlbertaBot) | Strategy / Economy / Military / Movement managers |
| Build-Order Search (BOSS; makespan/ROI) | base + upgrade ladder scheduling toward an economy goal |
| Combat sim (SparCraft; portfolio greedy / UCTCD) | exact number-based per-region combat resolution |
| Terrain analysis (BWEM/BWTA: regions, chokes, bases) | the given adjacency graph + Floyd-Warshall (already in harness) |
| Strategy portfolio + bandit selection | high-level posture switching (rush / expand / turtle) |
| Deep RL (AlphaStar) | reference only — training infra out of scope |

Key asymmetry vs StarCraft: only the opponent's **gold** and **this-turn submission** are
hidden; everything else (unit positions/HP, buildings) is known. So opponent modeling reduces
to a mirror/heuristic of intent, not fog-of-war inference.

---

## 3. Reference library

### Meta / curated lists (start here)
- [awesome-starcraftAI (SKTBrain)](https://github.com/SKTBrain/awesome-starcraftAI) — curated index of bots, papers, frameworks. *Use:* jump-off point.
- [Awesome-Broodwar-Resources](https://github.com/TheEngineeringBay/Awesome-Broodwar-Resources) — community resources, tutorials.
- [Dave Churchill — StarCraft AI resources](https://davechurchill.ca/starcraft/resources/) — the AIIDE organizer's hub (papers, code, talks).
- [Jay Scott's StarCraft AI blog](http://satirist.org/ai/starcraft/blog/) — Steamhammer author; the most practical bot-building writeups (chokes/regions, pathing, strategy selection).
- [Liquipedia Brood War bots](https://liquipedia.net/starcraft/Category:Bots) — per-bot histories/strengths.

### Competitions & ecosystem
- [AIIDE StarCraft AI Competition](https://liquipedia.net/starcraft/Artificial_Intelligence_and_Interactive_Digital_Entertainment_StarCraft_AI_Competition) — since 2010 (Buro/Churchill); full-game bots. *Use:* benchmark of what full-game macro+micro looks like.
- [SSCAIT](https://sscaitournament.com/) — live student ladder/stream.
- [BWAPI](https://github.com/bwapi/bwapi) — the C++ Brood War API every BW bot builds on.

### Foundational frameworks (architecture templates)
- [UAlbertaBot](https://github.com/davechurchill/ualbertabot) ([wiki](https://github.com/davechurchill/ualbertabot/wiki)) — **the** reference manager-decomposition bot. *Use:* copy the manager split (Strategy/Production/Combat/Scout/Worker/Building) → §4(a).
- [CommandCenter](https://github.com/davechurchill/commandcenter) — UAlbertaBot's architecture cleaned up for easy forking (BW+SC2). *Use:* clearest template to mirror.
- [python-sc2 (BurnySc2)](https://github.com/BurnySc2/python-sc2) — very readable macro bots. *Use:* idiomatic economy/army-management patterns.
- [PySC2 / SC2LE (DeepMind)](https://github.com/google-deepmind/pysc2) — RL env. *Use:* reference only.

### Reference bots (open source, strong — study macro/army logic)
- [PurpleWave](https://github.com/dgant/PurpleWave) (Scala; #1 BASIL/SSCAIT/SAIL) — strongest active BW bot; rich strategy + squad system. *Use:* high-water mark for macro decisions.
- [Stardust](https://github.com/bmnielsen/Stardust) (C++ Protoss) / [Locutus](https://github.com/bmnielsen/Locutus) — strong recent C++ bots.
- [Steamhammer](http://satirist.org/ai/starcraft/steamhammer/) (C++ Zerg, UAlbertaBot fork) — paired with the blog above; great for learning a real codebase.
- [SAIDA](https://github.com/TeamSAIDA/SAIDA) (Samsung SDS; 2018 AIIDE winner) — rule-base + some ML.

### Core technique papers
- [Survey of RTS Game AI in StarCraft (Ontañón et al., 2013)](https://inria.hal.science/hal-00871001/en) — the field map: tasks, methods, competition results. **Read first.**
- [Build Order Optimization in StarCraft / BOSS (Churchill & Buro, AIIDE 2011)](https://davechurchill.ca/publications/pdf/aiide11-bo.pdf) — feeds §5 build-order scheduler.
- [Robust Continuous Build-Order Optimization (Churchill, CoG 2019)](https://ieee-cog.org/2019/papers/paper_85.pdf) — replanning under change.
- [Portfolio Greedy Search for large-scale combat (Churchill & Buro, 2013)](https://www.semanticscholar.org/paper/f78a522c99cd3056dbfbbeae9d3410f08ab12027) — feeds §5 combat resolver.
- [Script- and cluster-based UCT (Justesen et al., 2014)](http://julian.togelius.com/Justesen2014Script.pdf) — search over scripts, not raw actions.
- [SparCraft combat sim](https://github.com/davechurchill/ualbertabot/wiki/SparCraft-Home) — the (approximate) BW combat simulator; we make ours exact.
- [Heuristic Search Techniques for RTS Games (Churchill PhD thesis)](https://davechurchill.ca/publications/pdf/DavidChurchill_phd_thesis.pdf) — unifies BOSS + combat search; **single best deep read.**
- [High-Level Strategy Selection under Partial Observability (2018)](https://arxiv.org/abs/1811.08568) + [Combinatorial Multi-armed Bandits for RTS](https://arxiv.org/pdf/1710.04805) — feeds §4(d) portfolio/bandit.
- [Terrain Analysis: choke detection & region decomposition (Perkins)](https://ojs.aaai.org/index.php/AIIDE/article/view/12405) · [BWEM lib](https://bwem.sourceforge.net/) · [BWTA2](https://github.com/s7jones/bwta2) — we already get regions/distances from the harness, so these are conceptual.

### Deep RL (reference only — infra out of scope)
- [AlphaStar — Grandmaster in SC2 (Nature 2019)](https://www.nature.com/articles/s41586-019-1724-z) ([blog](https://deepmind.google/blog/alphastar-grandmaster-level-in-starcraft-ii-using-multi-agent-reinforcement-learning/)) — multi-agent league self-play.
- [SC2 Build-Order Optimization via DRL + MCTS](https://arxiv.org/pdf/2006.10525).

---

## 4. Candidate architectures to test

All reuse §5. Listed roughly easiest→hardest.

**(a) Manager decomposition** — *idea:* split into Strategy/Economy/Military/Movement
managers; a Strategy manager sets a posture + a build-order *goal*, sub-managers fill commands
greedily by priority over one gold budget. *Refs:* UAlbertaBot, CommandCenter, PurpleWave.
*Pros:* legible, fast, easy to debug. *Cons:* decisions are reactive, not lookahead.

**(b) Exact-sim search** — *idea:* exploit the exact forward model. Macro = build-order search
(BOSS) toward an economy/army goal; tactics = combat search (portfolio greedy / UCTCD) over
the exact combat resolver. *Refs:* BOSS papers, Portfolio Greedy, Churchill thesis, Script-UCT.
*Pros:* highest ceiling; uses the game's unique determinism. *Cons:* more complex; must respect
the 100ms/turn budget.

**(c) Hybrid** *(natural target)* — *idea:* manager skeleton from (a), but the few pivotal
decisions (attack/expand/tech timing, train count) are chosen by simulating a handful of
candidate plans K days forward with the exact simulator + an eval function, then picking the
best. *Refs:* (a)+(b). *Pros:* pragmatic; lookahead only where it pays. *Cons:* needs a clean
plan-enumeration + eval seam.

**(d) Strategy portfolio + bandit** — *idea:* a set of scripted whole-game strategies; pick via
UCB / opponent model. *Refs:* strategy-selection + combinatorial-bandit papers. *Pros:* simple
meta-layer. *Cons:* weak fit — opponents vary per match and there's no cross-game memory in a
single contest run; more useful as an in-game posture switch than a between-game learner.

---

## 5. Shared reusable building blocks

Implement once; every architecture in §4 reuses these. Source of truth for all rule timing is
the referee's `run_game` order:
`apply_upgrades → apply_moves → apply_train_charge → apply_day_movement → spawn_trained →
apply_day_combat → apply_day_siege → apply_evening_work → apply_evening_upkeep`.

**Exact forward simulator** — *what:* advance `(state, bothPlayers' commands) → nextState`
exactly per the order above. *NEXT NATION:* port the referee's resolution (build/move/train →
movement step → combat focus-fire (lowest-hp then lowest-id; turrets add attacks; idle attacks
siege buildings) → work income `15·min(warriors,work_cap)` → upkeep `2`/warrior in id order,
starve = −1hp). The harness `read_turn_result` already encodes most of this for our own side;
the simulator generalizes it to hypothetical commands for both sides. This is the keystone —
build and unit-test it against referee logs first.

**Build-order / economy ROI scheduler** (BOSS) — *what:* given a goal (e.g. "income ≥ X" or
"N warriors by day D"), find the fastest action sequence. *Algorithm:* DFS + branch-and-bound
on **makespan**; don't tick frame-by-frame — **fast-forward** to the next time a precondition
+ gold are met; use macro-actions to keep the branching small; naive plan gives the initial
bound. *NEXT NATION:* actions = build base (300, needs a warrior on an empty stronghold) /
upgrade HQ or base / train; project gold as `+15·laborers −2·warriors` per day; respect
one-build-per-region-per-day and HQ `train_cap`.

**Combat resolver / portfolio** — *what:* will I win this region fight, and with what force?
*Algorithm:* exact resolver runs the focus-fire loop to the end; for force allocation use
portfolio-greedy hill-climbing over per-region commit sizes with playout evaluation. *NEXT
NATION:* because resolution is exact, "do I win region R?" is a deterministic function of (my
count, enemy count, hp profiles, turret) — commit `> defenders + turret + margin` to win the
attrition race (Lanchester); never dribble.

**Region / terrain utilities** — *what:* distances, expansion order, frontier, ownership zones.
*NEXT NATION:* reuse the harness `calculate_paths` (Floyd-Warshall `dist[][]`/`nxt[][]`, the
exact rule's "min sum-of-ceil-euclid, tie→smaller id" path). Classify each stronghold by
`dist(myHQ,·)` vs `dist(oppHQ,·)` → mine / contested / enemy; expansion order = nearest-first;
mirror map `r ↔ N-1-r` for symmetry.

**Evaluation function** — *what:* score a simulated state for the search/hybrid layers.
*NEXT NATION:* `w1·(myHQ_hp − enemyHQ_hp) + w2·economy(income or labor-saturation) +
w3·(#regions where I hold a numeric edge) + w4·army_value − w5·overproduction`. Terminal:
HQ destroyed = ±∞; at day 200 use the HP tiebreak. Weights are the tuning surface.

**Opponent model** — *what:* fill in the opponent's hidden intent for lookahead. *NEXT NATION:*
full state is known; only gold + this-turn submission are hidden. Default model = point-symmetry
mirror of our own policy (map symmetric so a mirror is a strong prior), or a cheap heuristic
(defend threatened buildings, expand nearest, mass-and-push). Estimate enemy gold by simulating
their observed costs/income.

---

## 6. Status & next

- Reference library: this doc.
- Architectures: none built yet (v1 manager prototype was removed; we rebuild from §4 on top
  of §5). Build order suggestion: §5 **exact simulator** first (keystone), then §4(a) as a
  baseline, then layer §4(c) hybrid search.
- Testing: existing referee + `viewer.html`; per project preference, smoke-test ONE battle per
  change, not repeated batches.

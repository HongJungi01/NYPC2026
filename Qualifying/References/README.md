# NEXT NATION — bot AI reference library

Reference material for building **NEXT NATION** bots (game rules: [../Rule.md](../Rule.md)),
drawn from how AIs play StarCraft and other similar games. Written for two readers: **the user**
(scannable maps of the field) and **Claude** (actionable algorithm summaries + NEXT NATION
mapping, so implementation can start from the doc). Each English doc has a Korean reading copy
(`*.ko.md`) with identical content — keep the pair in sync when editing.

## Documents

| Doc | What it is |
|---|---|
| [StarCraft.md](StarCraft.md) · [한글](StarCraft.ko.md) | StarCraft-playing AI: competitions, frameworks (UAlbertaBot…), bots, technique papers (BOSS, SparCraft…), the candidate **architectures**, and the **shared building blocks** (exact forward simulator, build-order/ROI scheduler, combat resolver, region utils, evaluator, opponent model). |
| [SimilarGames.md](SimilarGames.md) · [한글](SimilarGames.ko.md) | Non-StarCraft analogues — Planet Wars, Generals.io, microRTS, Halite/Kore/Lux, Battlecode — and the **cross-domain techniques** (RHEA, beam/SA search, multi-unit MCTS, Hungarian matching, influence maps, temporal ROI, logistics). |
| [GeminiReference.md](GeminiReference.md) | Gemini's cross-domain synthesis that motivated widening past StarCraft (Korean). |

## How to use

1. Orient: skim [SimilarGames.md](SimilarGames.md) §1 (why this game's traits favor forward-sim
   search) and §4 (recommended direction).
2. Pick an architecture to build (StarCraft.md §4: manager / exact-sim search / hybrid / portfolio).
3. Pull the **shared building blocks** it needs from StarCraft.md §5 — the **exact forward
   simulator** is the keystone for every search-based approach; build & log-test it first.
4. For the decision brain, pull the relevant technique from SimilarGames.md §3 (RHEA / beam /
   MCTS / influence map / Hungarian / temporal ROI) and its NEXT NATION mapping.

## Status

References only; no bot built yet. Recommended build order: exact forward simulator → a manager
baseline → layer forward-sim heuristic search (RHEA/beam). Testing convention: smoke-test ONE
battle per change via the referee ([../Seongho/files_3/testing-tool-practice.py](../Seongho/files_3/testing-tool-practice.py))
+ `viewer.html`; no repeated batches.

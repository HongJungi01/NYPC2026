// NEXT NATION — clean-room rebuild (redesign/남기고자_하는_기능.md).
//
// PHASE 1: verified foundation only. Ported byte-exact from submission/07031646r.cpp
// (확실히 남김: simulator / combat resolver / world-model I-O / SIM-OBS harness).
// Decision layer is intentionally ABSENT and rebuilt incrementally on top (Phase 2+):
//   - forced moves (defense / finish) = precise resolver argmax
//   - free move (surplus investment) = shallow rollout + value-function eval
// The live main here emits a valid do-nothing turn until the decision layer lands.
//
// Build:
//   g++ -O2 -std=c++17 redesign/bot.cpp -o bot.exe            # contest bot
//   g++ -O2 -std=c++17 -DSIM_TEST redesign/bot.cpp -o simtest # simulator gate
//   g++ -O2 -std=c++17 -DOBS_TEST redesign/bot.cpp -o obstest # world-model gate
//
// The simulator is ported 1:1 from the referee (testing-tool-practice.py, run_game
// order). SIM_TEST replays a referee log and asserts the result block byte-for-byte;
// OBS_TEST asserts the reconstructed observable state EXACT (opp gold estimate reported).

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

constexpr int MAX_TURN = 200;
constexpr int START_GOLD = 500;
constexpr int START_WARRIORS = 3;
constexpr int MOVE_COST = 10;
constexpr int TRAIN_COST = 120;
constexpr int BASE_BUILD_COST = 300;
constexpr int WORK_INCOME = 15;
constexpr int UPKEEP_PER_WARRIOR = 2;
constexpr int HQ_MAX_LEVEL = 5;
constexpr int BASE_MAX_LEVEL = 3;
constexpr int HQ_HEAL_COST = 1000;
constexpr int BASE_HEAL_COST = 500;

// Whole-game token bank (초읽기), parsed from the referee's TIME result line by
// apply_result_block. Foundation I/O state; the decision layer (rebuilt) reads it.
static int g_tokens_left = 5;

// Per-turn wall clock (벽시계 자율보정: 어느 머신이든 그 머신의 실제 경과를 재므로
// 대회 서버가 느려도 동일 ms 예산으로 컷 -> TLE 방지). main이 턴 시작마다 리셋.
static std::chrono::steady_clock::time_point g_turn_start;
static inline double elapsed_ms() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_turn_start).count();
}

struct HqLevelEntry {
  int upgrade_cost, warrior_hp, hp, turret, train_cap, work_cap;
};
struct BaseLevelEntry {
  int cost, hp, turret, work_cap;
};

constexpr HqLevelEntry HQ_LEVELS[HQ_MAX_LEVEL + 1] = {
    {0, 0, 0, 0, 0, 0},     {0, 4, 10, 1, 1, 1},    {600, 5, 15, 2, 1, 2},
    {1200, 6, 20, 2, 2, 3}, {2400, 7, 25, 3, 2, 4}, {3600, 8, 30, 3, 3, 5},
};
constexpr BaseLevelEntry BASE_LEVELS[BASE_MAX_LEVEL + 1] = {
    {0, 0, 0, 0},
    {300, 6, 1, 1},
    {600, 12, 1, 2},
    {1000, 18, 2, 3},
};

enum class Side : int { LEFT = 0, RIGHT = 1 };
enum class BType : int { HQ, BASE };
enum class WState : int { STATIONARY, MOVING };

inline Side opposite(Side s) { return s == Side::LEFT ? Side::RIGHT : Side::LEFT; }
inline char side_char(Side s) { return s == Side::LEFT ? 'A' : 'B'; }
inline Side parse_side_char(char c) { return c == 'A' ? Side::LEFT : Side::RIGHT; }

struct WarriorId {
  Side side = Side::LEFT;
  int num = 0;
  bool operator==(const WarriorId &o) const { return side == o.side && num == o.num; }
};

struct Warrior {
  WarriorId id;
  int region = 0;
  int hp = 0;
  WState state = WState::STATIONARY;
  int target = 0;
};

struct Building {
  int region = 0;
  Side side = Side::LEFT;
  BType type = BType::HQ;
  int level = 1;
  int hp = 10;

  int max_hp_now() const {
    return type == BType::HQ ? HQ_LEVELS[level].hp : BASE_LEVELS[level].hp;
  }
  int work_cap() const {
    return type == BType::HQ ? HQ_LEVELS[level].work_cap : BASE_LEVELS[level].work_cap;
  }
  int turret() const {
    return type == BType::HQ ? HQ_LEVELS[level].turret : BASE_LEVELS[level].turret;
  }
  int max_level() const { return type == BType::HQ ? HQ_MAX_LEVEL : BASE_MAX_LEVEL; }
  int heal_cost() const { return type == BType::HQ ? HQ_HEAL_COST : BASE_HEAL_COST; }
  int next_level_cost() const {
    if (level >= max_level()) return -1;
    return type == BType::HQ ? HQ_LEVELS[level + 1].upgrade_cost
                             : BASE_LEVELS[level + 1].cost;
  }
};

struct GameMap {
  int N = 0, K = 0;
  std::vector<long long> x, y;
  std::vector<int> strongholds; // sorted
  std::vector<std::vector<int>> adj;

  Side my_side = Side::LEFT;
  int my_hq = 0;
  int opp_hq = 0;
};

// =============================== shortest paths ==============================
// Integer Floyd-Warshall (matches the referee's integer dijkstra exactly, incl.
// tie-break: next step = argmin over adj (sorted asc) of edge+dist, ties -> id).

struct Paths {
  std::vector<std::vector<long long>> dist;
  std::vector<std::vector<int>> nxt;
};

static const long long PINF = LLONG_MAX / 4;

static long long euclid_ceil(const GameMap &M, int u, int v) {
  long long dx = M.x[u] - M.x[v];
  long long dy = M.y[u] - M.y[v];
  return (long long)std::ceil(std::sqrt((double)(dx * dx + dy * dy)));
}

static Paths calculate_paths(const GameMap &M) {
  Paths P;
  P.dist.assign(M.N, std::vector<long long>(M.N, PINF));
  P.nxt.assign(M.N, std::vector<int>(M.N, -1));
  for (int i = 0; i < M.N; ++i) {
    P.dist[i][i] = 0;
    P.nxt[i][i] = i;
  }
  for (int u = 0; u < M.N; ++u)
    for (int v : M.adj[u]) {
      long long w = euclid_ceil(M, u, v);
      if (w < P.dist[u][v]) P.dist[u][v] = w;
    }
  for (int k = 0; k < M.N; ++k)
    for (int u = 0; u < M.N; ++u) {
      if (P.dist[u][k] >= PINF) continue;
      for (int v = 0; v < M.N; ++v) {
        long long cand = P.dist[u][k] + P.dist[k][v];
        if (cand < P.dist[u][v]) P.dist[u][v] = cand;
      }
    }
  for (int u = 0; u < M.N; ++u)
    for (int v = 0; v < M.N; ++v) {
      if (u == v || P.dist[u][v] >= PINF) continue;
      long long best = PINF;
      for (int nb : M.adj[u]) { // adj sorted asc -> ties resolve to smaller id
        if (P.dist[nb][v] >= PINF) continue;
        long long score = euclid_ceil(M, u, nb) + P.dist[nb][v];
        if (score < best) {
          best = score;
          P.nxt[u][v] = nb;
        }
      }
    }
  return P;
}

static bool reachable(const Paths &P, int u, int v) { return P.dist[u][v] < PINF; }

// ============================ exact forward simulator ========================
// Two-sided world the simulator mutates. gold[side], warriors+buildings for both.

struct SimState {
  long long gold[2] = {START_GOLD, START_GOLD};
  std::vector<Warrior> warriors;  // both sides
  std::vector<Building> buildings; // both sides
  int next_suffix[2] = {START_WARRIORS + 1, START_WARRIORS + 1};
};

struct Cmds {
  int train_n = 0;                            // 0 = no train
  std::vector<std::pair<int, int>> moves;     // (suffix, target)
  std::vector<int> upgrades;                  // regions
};

// Result block, same fields/semantics as the referee's, for unit testing.
// key encoding matches referee: LEFT = -suffix, RIGHT = +suffix.
static inline int wkey(Side s, int suffix) {
  return s == Side::LEFT ? -suffix : suffix;
}
static inline Side side_from_key(int k) { return k < 0 ? Side::LEFT : Side::RIGHT; }
static inline int suffix_from_key(int k) { return k < 0 ? -k : k; }
static inline std::string id_str(int k) {
  return std::string(1, side_from_key(k) == Side::LEFT ? 'A' : 'B') +
         std::to_string(suffix_from_key(k));
}
// sort key matching referee wkey_sort: (side_value, suffix)
static inline std::pair<int, int> wkey_sort(int k) {
  return {(int)side_from_key(k), suffix_from_key(k)};
}

struct ResultBlock {
  std::vector<int> trained;                       // keys
  std::vector<std::pair<int, Side>> upgrades;     // (region, side)
  std::vector<std::pair<int, int>> moves;         // (key, new_region)
  std::vector<std::tuple<int, int, int>> damages; // (cause_idx, key, dmg)
  std::vector<std::tuple<Side, int, int>> sieges; // (side, region, dmg)
};
// cause_idx: 0=TURRET 1=COMBAT 2=HUNGER  (matches referee CAUSE_ORDER)
static const char *CAUSE_NAME[3] = {"TURRET", "COMBAT", "HUNGER"};

static int hq_region_of(const GameMap &M, Side s) {
  return s == Side::LEFT ? 0 : M.N - 1;
}

static Building *building_at(SimState &st, int region) {
  for (auto &b : st.buildings)
    if (b.region == region) return &b;
  return nullptr;
}
static const Building *building_at(const SimState &st, int region) {
  for (const auto &b : st.buildings)
    if (b.region == region) return &b;
  return nullptr;
}
static Building *hq_of(SimState &st, Side s) {
  for (auto &b : st.buildings)
    if (b.type == BType::HQ && b.side == s) return &b;
  return nullptr;
}
static Warrior *warrior_by_suffix(SimState &st, Side s, int suffix) {
  for (auto &w : st.warriors)
    if (w.id.side == s && w.id.num == suffix) return &w;
  return nullptr;
}
static bool enemy_warrior_at(const SimState &st, int region, Side s) {
  for (const auto &w : st.warriors)
    if (w.hp > 0 && w.region == region && w.id.side != s) return true;
  return false;
}

static SimState init_sim(const GameMap &M) {
  SimState st;
  st.buildings.push_back({0, Side::LEFT, BType::HQ, 1, HQ_LEVELS[1].hp});
  st.buildings.push_back({M.N - 1, Side::RIGHT, BType::HQ, 1, HQ_LEVELS[1].hp});
  for (int sfx = 1; sfx <= START_WARRIORS; ++sfx) {
    st.warriors.push_back({{Side::LEFT, sfx}, 0, HQ_LEVELS[1].warrior_hp});
    st.warriors.push_back({{Side::RIGHT, sfx}, M.N - 1, HQ_LEVELS[1].warrior_hp});
  }
  return st;
}

// --- morning: upgrades / moves / train charge (per side; left then right) ----
// `strict` (replay) trusts the log; lookahead also passes valid cmds, so we
// apply effects without WA validation but still honour the build/upgrade/heal
// branch + gold like the referee.
static void apply_upgrades(SimState &st, const GameMap &M, Side s,
                           const Cmds &c, ResultBlock &rb) {
  int u = (int)s;
  for (int r : c.upgrades) {
    Building *b = building_at(st, r);
    if (b == nullptr) {
      st.gold[u] -= BASE_BUILD_COST;
      st.buildings.push_back({r, s, BType::BASE, 1, BASE_LEVELS[1].hp});
    } else if (b->level >= b->max_level()) {
      st.gold[u] -= b->heal_cost();
      b->hp = b->max_hp_now();
    } else {
      st.gold[u] -= b->next_level_cost();
      b->level += 1;
      b->hp = b->max_hp_now();
    }
    rb.upgrades.push_back({r, s});
  }
}

static void apply_moves(SimState &st, const GameMap &M, Side s, const Cmds &c) {
  int u = (int)s;
  for (auto [suffix, target] : c.moves) {
    Warrior *w = warrior_by_suffix(st, s, suffix);
    if (!w) continue;
    const Building *b = building_at(st, target);
    int cost = (b && b->side == s) ? 0 : MOVE_COST;
    st.gold[u] -= cost;
    w->state = WState::MOVING;
    w->target = target;
  }
}

static int apply_train_charge(SimState &st, Side s, const Cmds &c) {
  if (c.train_n <= 0) return 0;
  Building *hq = hq_of(st, s);
  if (!hq) return 0;
  int cap = HQ_LEVELS[hq->level].train_cap;
  int n = std::min(c.train_n, cap);
  st.gold[(int)s] -= (long long)TRAIN_COST * n;
  return n;
}

// --- day: movement (one step), then spawn ------------------------------------
static void apply_day_movement(SimState &st, const GameMap &M, const Paths &P,
                               ResultBlock &rb) {
  // enemy presence per side, frozen from pre-movement positions.
  std::set<int> enemy_of_left, enemy_of_right;
  for (const auto &w : st.warriors) {
    if (w.hp <= 0) continue;
    if (w.id.side == Side::RIGHT) enemy_of_left.insert(w.region);
    else enemy_of_right.insert(w.region);
  }
  for (auto &w : st.warriors) {
    if (w.hp <= 0 || w.state != WState::MOVING) continue;
    int t = w.target;
    if (w.region == t) {
      w.state = WState::STATIONARY;
      continue;
    }
    const std::set<int> &es = (w.id.side == Side::LEFT) ? enemy_of_left : enemy_of_right;
    if (es.count(w.region)) continue; // pinned by enemy
    int nv = P.nxt[w.region][t];
    if (nv < 0) continue;
    w.region = nv;
    rb.moves.push_back({wkey(w.id.side, w.id.num), nv});
    if (w.region == t) w.state = WState::STATIONARY;
  }
}

static void spawn_trained(SimState &st, const GameMap &M, Side s, int n,
                          ResultBlock &rb) {
  if (n <= 0) return;
  int u = (int)s;
  Building *hq = hq_of(st, s);
  int region = hq->region;
  int whp = HQ_LEVELS[hq->level].warrior_hp;
  for (int i = 0; i < n; ++i) {
    int sfx = st.next_suffix[u]++;
    st.warriors.push_back({{s, sfx}, region, whp});
    rb.trained.push_back(wkey(s, sfx));
  }
}

// --- day: combat -------------------------------------------------------------
// One focus-fire tick against `side`'s warriors in `region`: lowest hp, ties ->
// lowest suffix. Returns the hit warrior's key, or 0 if none (idle attack).
static int damage_tick(SimState &st, int region, Side side) {
  Warrior *best = nullptr;
  for (auto &w : st.warriors) {
    if (w.region != region || w.id.side != side || w.hp <= 0) continue;
    if (!best || w.hp < best->hp ||
        (w.hp == best->hp && w.id.num < best->id.num))
      best = &w;
  }
  if (!best) return 0;
  best->hp -= 1;
  return wkey(best->id.side, best->id.num);
}

static void apply_day_combat(SimState &st, const GameMap &M, ResultBlock &rb,
                             std::map<int, int> &siege_damage) {
  // regions of interest: any with a hp>0 warrior, or a building.
  std::set<int> regions;
  for (const auto &w : st.warriors)
    if (w.hp > 0) regions.insert(w.region);
  for (const auto &b : st.buildings) regions.insert(b.region);

  std::map<std::pair<int, int>, int> dmg_accum; // (cause_idx, key) -> dmg

  for (int region : regions) { // std::set iterates ascending
    int lc = 0, rc = 0;
    for (const auto &w : st.warriors) {
      if (w.hp <= 0 || w.region != region) continue;
      if (w.id.side == Side::LEFT) lc++;
      else rc++;
    }
    const Building *b = building_at(st, region);
    bool b_left = b && b->side == Side::LEFT;
    bool b_right = b && b->side == Side::RIGHT;
    int turret = b ? b->turret() : 0;

    bool left_present = lc > 0 || b_left;
    bool right_present = rc > 0 || b_right;
    if (!(left_present && right_present)) continue;

    int left_cap = lc + (b_left ? turret : 0);
    int right_cap = rc + (b_right ? turret : 0);

    int left_idle = 0;
    for (int i = 0; i < left_cap; ++i) {
      int cause = (b_left && i < turret) ? 0 : 1;
      int k = damage_tick(st, region, Side::RIGHT);
      if (k == 0) left_idle++;
      else dmg_accum[{cause, k}]++;
    }
    int right_idle = 0;
    for (int i = 0; i < right_cap; ++i) {
      int cause = (b_right && i < turret) ? 0 : 1;
      int k = damage_tick(st, region, Side::LEFT);
      if (k == 0) right_idle++;
      else dmg_accum[{cause, k}]++;
    }
    if (b) {
      int attacker_idle = (b->side == Side::LEFT) ? right_idle : left_idle;
      if (attacker_idle > 0) siege_damage[region] = attacker_idle;
    }
  }
  for (auto &[ck, dmg] : dmg_accum)
    rb.damages.push_back({ck.first, ck.second, dmg});
}

static void apply_day_siege(SimState &st, ResultBlock &rb,
                            std::map<int, int> &siege_damage) {
  std::vector<int> regions;
  for (const auto &b : st.buildings) regions.push_back(b.region);
  std::sort(regions.begin(), regions.end());
  std::set<int> destroyed;
  for (int r : regions) {
    auto it = siege_damage.find(r);
    if (it == siege_damage.end() || it->second <= 0) continue;
    Building *b = building_at(st, r);
    int dealt = std::min(it->second, b->hp);
    b->hp -= dealt;
    rb.sieges.push_back({b->side, r, dealt});
    if (b->hp <= 0) destroyed.insert(r);
  }
  st.buildings.erase(std::remove_if(st.buildings.begin(), st.buildings.end(),
                                    [&](const Building &b) {
                                      return destroyed.count(b.region);
                                    }),
                     st.buildings.end());
}

// --- evening: work income, then upkeep --------------------------------------
static void apply_evening_work(SimState &st) {
  std::vector<int> regions;
  for (const auto &b : st.buildings) regions.push_back(b.region);
  std::sort(regions.begin(), regions.end());
  for (int r : regions) {
    Building *b = building_at(st, r);
    int cnt = 0;
    for (const auto &w : st.warriors)
      if (w.hp > 0 && w.region == r && w.id.side == b->side) cnt++;
    int take = std::min(b->work_cap(), cnt);
    st.gold[(int)b->side] += (long long)WORK_INCOME * take;
  }
}

static void apply_evening_upkeep(SimState &st, ResultBlock &rb) {
  for (int sv = 0; sv < 2; ++sv) {
    Side s = (Side)sv;
    std::vector<Warrior *> alive;
    for (auto &w : st.warriors)
      if (w.id.side == s && w.hp > 0) alive.push_back(&w);
    std::sort(alive.begin(), alive.end(),
              [](const Warrior *a, const Warrior *b) { return a->id.num < b->id.num; });
    for (Warrior *w : alive) {
      if (st.gold[sv] >= UPKEEP_PER_WARRIOR) {
        st.gold[sv] -= UPKEEP_PER_WARRIOR;
      } else {
        w->hp -= 1;
        rb.damages.push_back({2, wkey(s, w->id.num), 1});
      }
    }
  }
  st.warriors.erase(std::remove_if(st.warriors.begin(), st.warriors.end(),
                                   [](const Warrior &w) { return w.hp <= 0; }),
                    st.warriors.end());
}

// One full day, exact referee order. Returns the merged result block.
static ResultBlock step(SimState &st, const GameMap &M, const Paths &P,
                        const Cmds &left, const Cmds &right) {
  ResultBlock rb;
  apply_upgrades(st, M, Side::LEFT, left, rb);
  apply_moves(st, M, Side::LEFT, left);
  int tl = apply_train_charge(st, Side::LEFT, left);
  apply_upgrades(st, M, Side::RIGHT, right, rb);
  apply_moves(st, M, Side::RIGHT, right);
  int tr = apply_train_charge(st, Side::RIGHT, right);

  apply_day_movement(st, M, P, rb);
  spawn_trained(st, M, Side::LEFT, tl, rb);
  spawn_trained(st, M, Side::RIGHT, tr, rb);

  std::map<int, int> siege_damage;
  apply_day_combat(st, M, rb, siege_damage);
  apply_day_siege(st, rb, siege_damage);
  apply_evening_work(st);
  apply_evening_upkeep(st, rb);
  return rb;
}

// Render a result block to the referee's log line format (canonical order), so
// SIM_TEST can compare line-for-line against write_turn_result output.
static std::vector<std::string> render_result(ResultBlock rb) {
  std::vector<std::string> out;
  std::sort(rb.upgrades.begin(), rb.upgrades.end(),
            [](auto &a, auto &b) { return a.first < b.first; });
  for (auto &[r, s] : rb.upgrades)
    out.push_back("UPGRADE " + std::string(1, side_char(s)) + " " + std::to_string(r));

  if (!rb.trained.empty()) {
    std::sort(rb.trained.begin(), rb.trained.end(),
              [](int a, int b) { return wkey_sort(a) < wkey_sort(b); });
    std::string line = "TRAIN";
    for (int k : rb.trained) line += " " + id_str(k);
    out.push_back(line);
  }
  std::sort(rb.moves.begin(), rb.moves.end(),
            [](auto &a, auto &b) { return wkey_sort(a.first) < wkey_sort(b.first); });
  for (auto &[k, r] : rb.moves)
    out.push_back("MOVE " + id_str(k) + " " + std::to_string(r));

  std::sort(rb.damages.begin(), rb.damages.end(), [](auto &a, auto &b) {
    if (std::get<0>(a) != std::get<0>(b)) return std::get<0>(a) < std::get<0>(b);
    return wkey_sort(std::get<1>(a)) < wkey_sort(std::get<1>(b));
  });
  for (auto &[cause, k, d] : rb.damages)
    out.push_back("DAMAGE " + std::string(CAUSE_NAME[cause]) + " " + id_str(k) +
                  " " + std::to_string(d));

  std::sort(rb.sieges.begin(), rb.sieges.end(),
            [](auto &a, auto &b) { return std::get<1>(a) < std::get<1>(b); });
  for (auto &[s, r, d] : rb.sieges)
    out.push_back("SIEGE " + std::string(1, side_char(s)) + " " + std::to_string(r) +
                  " " + std::to_string(d));
  return out;
}

// ============================ §3 combat resolver =============================
// Exact single-region focus-fire attrition: starting from these warrior HP
// pools (attacker vs defender) with the defender's building turret adding to the
// defender's daily attacks, run day-rounds until one side's warriors are gone.
// Returns attacker survivors (>0 => attacker clears the garrison and can siege).
// R11 (53603): optional defender RETRAIN stream -- from round 1 (reaction delay)
// the defender gains `reinforce_per_round` fresh `reinforce_hp` warriors, up to
// `reinforce_total`, spawning into the same day's combat like the referee's
// morning train. A static-garrison read green-lit a 19-strong "winning" finish
// into an L4 HQ that retrained 20 defenders during the grind (zero siege dealt).
static int region_fight_survivors(std::vector<int> atk, std::vector<int> def,
                                  int def_turret, int reinforce_hp = 0,
                                  int reinforce_per_round = 0,
                                  int reinforce_total = 0) {
  auto tick = [](std::vector<int> &t) {
    int bi = -1;
    for (size_t i = 0; i < t.size(); ++i)
      if (t[i] > 0 && (bi < 0 || t[i] < t[bi])) bi = (int)i;
    if (bi >= 0) t[bi]--;
  };
  for (int guard = 0; guard < 1000; ++guard) {
    if (guard >= 1)
      for (int i = 0; i < reinforce_per_round && reinforce_total > 0; ++i) {
        def.push_back(reinforce_hp);
        reinforce_total--;
      }
    int a = 0, d = 0;
    for (int h : atk) a += (h > 0);
    for (int h : def) d += (h > 0);
    if (a == 0 || d == 0) break;
    int atk_cap = a, def_cap = d + def_turret;
    for (int i = 0; i < atk_cap; ++i) tick(def); // counts frozen this round
    for (int i = 0; i < def_cap; ++i) tick(atk);
  }
  int s = 0;
  for (int h : atk) s += (h > 0);
  return s;
}

// Min number of fresh attackers (each `atk_hp`) needed to clear a garrison.
static int min_mass_to_take(int atk_hp, const std::vector<int> &def_hps,
                            int turret, int cap, int reinforce_hp = 0,
                            int reinforce_per_round = 0, int reinforce_total = 0) {
  for (int m = 1; m <= cap; ++m) {
    std::vector<int> atk(m, atk_hp);
    if (region_fight_survivors(atk, def_hps, turret, reinforce_hp,
                               reinforce_per_round, reinforce_total) > 0)
      return m;
  }
  return cap + 1; // unwinnable within cap
}

// Min number of defenders (each `def_hp`), backed by our building `turret`,
// needed to wipe an incoming attack — i.e. leave zero attacker survivors so the
// building can't be sieged. Used by the anticipatory-defense mobilization.
static int min_defenders_to_hold(const std::vector<int> &atk_hps, int def_hp,
                                 int turret, int cap) {
  for (int d = 0; d <= cap; ++d) {
    std::vector<int> def(d, def_hp);
    if (region_fight_survivors(atk_hps, def, turret) == 0) return d;
  }
  return cap + 1; // cannot hold within cap
}

// ================================ §4 evaluation ==============================
static long long side_income(const SimState &st, Side s) {
  long long inc = 0;
  for (const auto &b : st.buildings) {
    if (b.side != s) continue;
    int cnt = 0;
    for (const auto &w : st.warriors)
      if (w.hp > 0 && w.region == b.region && w.id.side == s) cnt++;
    inc += (long long)WORK_INCOME * std::min(cnt, b.work_cap());
  }
  return inc;
}
// ================= world-model reconstruction (shared path) =================
// The live contest path AND the OBS_TEST harness both fold a referee result
// block (send format) into our world model via apply_result_block. Enemy units,
// counts, buildings and levels are reconstructed EXACTLY from the observed
// events; only opp gold is an estimate (see OppGoldTracker).

struct OppGoldTracker {
  std::set<int> moving; // opp suffixes mid move-run last turn (for move-cost timing)
};

static std::vector<std::string> rb_tokens(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  for (std::string t; is >> t;) out.push_back(t);
  return out;
}

// Charge our own commands, fold in BOTH sides' observed events, then evening
// income/upkeep. `me` is the viewpoint side; `next` yields the next input line
// (cin for live, a vector iterator for the harness). Exact except opp gold,
// whose only approximation is opp move-cost timing.
template <class NextLine>
static void apply_result_block(SimState &S, const GameMap &M, Side me,
                               const Cmds &mine, OppGoldTracker &gt, NextLine next) {
  // charge our own commands locally (gold is hidden by the referee).
  for (int r : mine.upgrades) {
    Building *b = building_at(S, r);
    if (!b) S.gold[(int)me] -= BASE_BUILD_COST;
    else if (b->level >= b->max_level()) S.gold[(int)me] -= b->heal_cost();
    else S.gold[(int)me] -= b->next_level_cost();
  }
  for (auto [suffix, target] : mine.moves) {
    const Building *b = building_at(S, target);
    int cost = (b && b->side == me) ? 0 : MOVE_COST;
    S.gold[(int)me] -= cost;
    if (Warrior *w = warrior_by_suffix(S, me, suffix)) {
      w->state = WState::MOVING;
      w->target = target;
    }
  }
  S.gold[(int)me] -= (long long)TRAIN_COST * mine.train_n;

  {
    std::string line = next();
    if (line == "FINISH") std::exit(0);
  }
  // TIME t_used_self tokens_self t_used_opp tokens_opp -- keep our remaining tokens
  // (the whole-game time bank) so strategy_manager knows how long it may think.
  {
    auto tt = rb_tokens(next());
    if (tt.size() >= 3) g_tokens_left = std::stoi(tt[2]);
  }
  // UPGRADE
  {
    auto t = rb_tokens(next());
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = rb_tokens(next());
      Side s = parse_side_char(r.at(0)[0]);
      int region = std::stoi(r.at(1));
      Building *b = building_at(S, region);
      int cost = !b ? BASE_BUILD_COST
                    : (b->level >= b->max_level() ? b->heal_cost()
                                                  : b->next_level_cost());
      if (s != me) S.gold[(int)s] -= cost; // opp estimate; mine charged above
      if (!b) {
        S.buildings.push_back({region, s, BType::BASE, 1, BASE_LEVELS[1].hp});
      } else {
        if (b->level >= b->max_level()) b->hp = b->max_hp_now();
        else { b->level += 1; b->hp = b->max_hp_now(); }
      }
    }
  }
  // TRAIN
  {
    auto t = rb_tokens(next());
    int n = std::stoi(t.at(1));
    if (n > 0) {
      auto ids = rb_tokens(next());
      for (int i = 0; i < n; ++i) {
        Side s = parse_side_char(ids.at(i)[0]);
        int suffix = std::stoi(ids.at(i).substr(1));
        int hqr = hq_region_of(M, s);
        Building *hqb = building_at(S, hqr);
        int lvl = hqb ? hqb->level : 1;
        if (s != me) S.gold[(int)s] -= TRAIN_COST; // opp estimate
        S.warriors.push_back({{s, suffix}, hqr, HQ_LEVELS[lvl].warrior_hp});
        S.next_suffix[(int)s] = std::max(S.next_suffix[(int)s], suffix + 1);
      }
    }
  }
  // MOVE
  {
    // Opp move-cost estimate. The referee charges MOVE_COST once per move
    // COMMAND, free if the command's TARGET is the mover's own building
    // (apply_moves), and forbids re-commanding a unit that is already moving. We
    // observe resulting positions, not commands or targets, so the per-command
    // cost is inferred from where units END UP. Two estimators:
    //
    //  - default (naive): charge at run start unless the FIRST observed step
    //    lands on an own building. Mis-charges multi-step moves INTO own buildings
    //    so opp gold runs LOW (opp looks poorer) -- which keeps the lookahead
    //    aggressive. This is the shipped estimator; it plays best.
    //  - OVERESTIMATE_OPP_GOLD: assume opp moves are own-base movement (FREE) and
    //    charge only when an opp unit SETTLES on a non-own, non-pinned region (a
    //    confirmed paid attack/expand arrival). Strictly OVER-estimates opp gold
    //    (OBS_TEST signed err >= 0). Idea was "richer opp = safer/robust", but a
    //    paired pool test REFUTED it: vs a fixed pool naive went 6-0-6 while this
    //    went 6-6-0 (0-4 vs the mgr5 economy bot) -- a richer opp model makes us
    //    timid and lose, same as USE_INFER. Kept, flagged, OFF by default, for the record.
    std::set<int> opp_cur_moved;
    auto t = rb_tokens(next());
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = rb_tokens(next());
      Side s = parse_side_char(r.at(0)[0]);
      int suffix = std::stoi(r.at(0).substr(1));
      int region = std::stoi(r.at(1));
      if (s != me) {
        opp_cur_moved.insert(suffix);
#ifndef OVERESTIMATE_OPP_GOLD
        if (!gt.moving.count(suffix)) {
          const Building *b = building_at(S, region);
          S.gold[(int)s] -= (b && b->side == s) ? 0 : MOVE_COST;
        }
#endif
      }
      if (Warrior *w = warrior_by_suffix(S, s, suffix)) {
        w->region = region;
        if (w->state == WState::MOVING && w->region == w->target)
          w->state = WState::STATIONARY;
      }
    }
#ifdef OVERESTIMATE_OPP_GOLD
    Side opp = opposite(me);
    std::set<int> still_moving = opp_cur_moved; // carry pinned (stalled) units
    auto pinned_by_me = [&](int region) {
      for (const auto &w2 : S.warriors)
        if (w2.id.side == me && w2.hp > 0 && w2.region == region) return true;
      return false;
    };
    for (int suffix : gt.moving) {
      if (opp_cur_moved.count(suffix)) continue;           // still advancing
      Warrior *w = warrior_by_suffix(S, opp, suffix);
      if (!w) continue;                                    // died en route -> free
      const Building *b = building_at(S, w->region);
      if (b && b->side == opp) continue;                   // settled on own building -> free
      if (pinned_by_me(w->region)) { still_moving.insert(suffix); continue; } // stall
      S.gold[(int)opp] -= MOVE_COST;                       // confirmed paid arrival
    }
    gt.moving = still_moving;
#else
    gt.moving = opp_cur_moved;
#endif
  }
  // DAMAGE
  {
    auto t = rb_tokens(next());
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = rb_tokens(next());
      Side s = parse_side_char(r.at(1)[0]);
      int suffix = std::stoi(r.at(1).substr(1));
      int dmg = std::stoi(r.at(2));
      if (Warrior *w = warrior_by_suffix(S, s, suffix)) w->hp -= dmg;
    }
    S.warriors.erase(std::remove_if(S.warriors.begin(), S.warriors.end(),
                                    [](const Warrior &w) { return w.hp <= 0; }),
                     S.warriors.end());
  }
  // SIEGE
  {
    auto t = rb_tokens(next());
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = rb_tokens(next());
      int region = std::stoi(r.at(1));
      int dmg = std::stoi(r.at(2));
      if (Building *b = building_at(S, region)) b->hp -= dmg;
    }
    S.buildings.erase(std::remove_if(S.buildings.begin(), S.buildings.end(),
                                     [](const Building &b) { return b.hp <= 0; }),
                      S.buildings.end());
  }
  (void)next(); // END

  // Evening income/upkeep for BOTH sides (exact for us; opp gold is an estimate
  // that approximates opp move costs, so it can run slightly high -> conservative).
  for (int sv = 0; sv < 2; ++sv) {
    Side s = (Side)sv;
    S.gold[sv] += side_income(S, s);
    int alive = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == s) alive++;
    S.gold[sv] = std::max(0LL, S.gold[sv] - (long long)UPKEEP_PER_WARRIOR * alive);
  }
}

// Serialize a ResultBlock into the referee's per-bot send format (counts +
// lines) as seen by side `me`. Used by the OBS_TEST harness to drive the exact
// same parsing path apply_result_block uses live. (Event order within a block
// does not affect the reconstructed state.)
static std::vector<std::string> to_send_block(const ResultBlock &rb, int day) {
  std::vector<std::string> out;
  out.push_back("TURN " + std::to_string(day));
  out.push_back("TIME 0 0 0 0");
  out.push_back("UPGRADE " + std::to_string(rb.upgrades.size()));
  for (auto &[r, s] : rb.upgrades)
    out.push_back(std::string(1, side_char(s)) + " " + std::to_string(r));
  out.push_back("TRAIN " + std::to_string(rb.trained.size()));
  if (!rb.trained.empty()) {
    std::string line;
    for (size_t i = 0; i < rb.trained.size(); ++i)
      line += (i ? " " : "") + id_str(rb.trained[i]);
    out.push_back(line);
  }
  out.push_back("MOVE " + std::to_string(rb.moves.size()));
  for (auto &[k, r] : rb.moves)
    out.push_back(id_str(k) + " " + std::to_string(r));
  out.push_back("DAMAGE " + std::to_string(rb.damages.size()));
  for (auto &[cause, k, d] : rb.damages)
    out.push_back(std::string(CAUSE_NAME[cause]) + " " + id_str(k) + " " +
                  std::to_string(d));
  out.push_back("SIEGE " + std::to_string(rb.sieges.size()));
  for (auto &[s, r, d] : rb.sieges)
    out.push_back(std::string(1, side_char(s)) + " " + std::to_string(r) + " " +
                  std::to_string(d));
  out.push_back("END");
  return out;
}

// ================================ contest I/O ================================
#if !defined(SIM_TEST) && !defined(OBS_TEST)

static std::string readln() {
  std::string s;
  if (!std::getline(std::cin, s)) std::exit(0);
  return s;
}
static std::vector<std::string> tokens(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  for (std::string t; is >> t;) out.push_back(t);
  return out;
}

static void parse_init(GameMap &M, SimState &S) {
  {
    auto t = tokens(readln());
    M.my_side = (t.at(1) == "LEFT") ? Side::LEFT : Side::RIGHT;
  }
  {
    auto t = tokens(readln());
    M.N = std::stoi(t.at(0));
    M.K = std::stoi(t.at(1));
  }
  M.x.assign(M.N, 0);
  M.y.assign(M.N, 0);
  {
    auto t = tokens(readln());
    for (int i = 0; i < M.N; ++i) M.x[i] = std::stoll(t.at(i));
  }
  {
    auto t = tokens(readln());
    for (int i = 0; i < M.N; ++i) M.y[i] = std::stoll(t.at(i));
  }
  {
    auto t = tokens(readln());
    for (auto &s : t) M.strongholds.push_back(std::stoi(s));
    std::sort(M.strongholds.begin(), M.strongholds.end());
  }
  M.adj.assign(M.N, {});
  for (int r = 0; r < M.N; ++r) {
    auto t = tokens(readln());
    int deg = std::stoi(t.at(0));
    for (int j = 0; j < deg; ++j) M.adj[r].push_back(std::stoi(t.at(1 + j)));
    std::sort(M.adj[r].begin(), M.adj[r].end());
  }
  M.my_hq = hq_region_of(M, M.my_side);
  M.opp_hq = hq_region_of(M, opposite(M.my_side));
  S = init_sim(M);
  std::cout << "OK" << std::endl;
}

static bool read_turn_start(int &turn) {
  std::string line = readln();
  if (line == "FINISH") return false;
  auto t = tokens(line);
  turn = std::stoi(t.at(2));
  return true;
}

// Update our world model S from the referee's result block for our submitted
// commands (the *observed* single source of truth each turn). Thin wrapper over
// the shared apply_result_block; the static tracker persists opp move-run state
// across turns for the opp-gold estimate.
static void read_turn_result(SimState &S, const GameMap &M, const Cmds &mine) {
  static OppGoldTracker gt;
  apply_result_block(S, M, M.my_side, mine, gt, readln);
}

static void emit_cmds(const GameMap &M, const Cmds &a) {
  std::cout << "COMMAND\n";
  for (auto [suffix, target] : a.moves)
    std::cout << "MOVE " << side_char(M.my_side) << suffix << ' ' << target << '\n';
  for (int r : a.upgrades) std::cout << "UPGRADE " << r << '\n';
  if (a.train_n > 0) std::cout << "TRAIN " << a.train_n << '\n';
  std::cout << "END" << std::endl;
}

// ===================== decision layer (clean-room, design-faithful) ==========
// Implements redesign/남기고자_하는_기능.md. Organic pipeline:
//   [토대] 경로예측(nxt역산+Rule169 교집합) -> 공격판별(목적지 role)+frontier
//     -> [강제] 수비 need-bound(리졸버 grind-window, 생산우선 HQ, 못지킬 기지 포기)
//     -> [강제] 끝내기(리졸버) + 타깃선정(가치/비용) + anti-trickle 발사 + 우회
//     -> [자유] 잉여 상황배분(w_def/w_atk/w_grow) -> 확장/생산/업글   [HP-인지 노동]

// Persistent cross-turn state. Rule 169 (이동잠금) makes an opp's target STABLE
// once moving, so path-prediction candidate sets can be INTERSECTED to narrow.
struct BotState {
  std::map<int, int> prev_opp;       // opp suffix -> region last turn
  std::map<int, std::set<int>> pred; // opp suffix -> candidate target regions
  int atk_target = -1;               // committed attack target (wave commitment, anti-trickle)
  int atk_deadline = -1;             // 웨이브 동시도착 목표 턴 (도착 스케줄러)
  int dbg_thr = 0, dbg_att = 0, dbg_inhq = 0, dbg_rush = 0; // BOTDBG 계측 (live 호출이 마지막)
};

// HP-aware partition: fill each building's work_cap with the LOWEST-hp stationary
// units (군 체력=재생불가 자원 -> 저HP는 노동, 고HP는 전투 보존). Rest = mobile.
static std::vector<const Warrior *> partition_units(const SimState &S, const GameMap &M,
                                                    std::set<int> &lab) {
  Side me = M.my_side;
  lab.clear();
  for (const auto &b : S.buildings) {
    if (b.side != me) continue;
    std::vector<const Warrior *> on;
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == b.region && w.state == WState::STATIONARY)
        on.push_back(&w);
    std::sort(on.begin(), on.end(), [](const Warrior *x, const Warrior *y) { return x->hp < y->hp; });
    for (int i = 0; i < b.work_cap() && i < (int)on.size(); ++i) lab.insert(on[i]->id.num);
  }
  std::vector<const Warrior *> mob;
  for (const auto &w : S.warriors)
    if (w.id.side == me && w.hp > 0 && w.state == WState::STATIONARY && !lab.count(w.id.num))
      mob.push_back(&w);
  return mob;
}

// Hop-count ETA along the referee shortest path (movement = 1 region/turn).
static long long turns_to(const Paths &P, int N, int a, int dest) {
  if (a == dest) return 0;
  if (!reachable(P, a, dest)) return PINF;
  int cur = a;
  long long n = 0;
  while (cur != dest && n <= N + 1) {
    cur = P.nxt[cur][dest];
    if (cur < 0) return PINF;
    n++;
  }
  return n;
}

// ===== value function (design: eval — Tier1 켬, Tier2=0) =====================
// 종말 잎 = 정확 승패(±). 비종말 잎 = turn-200 추정: HQ HP차(판정)·래더·병력·수입·
// 만기 골드. Tier2(위치/레이스 시계)는 아직 0.
static double eval_state(const SimState &s, const GameMap &M, Side me, int turn) {
  Side opp = opposite(me);
  const Building *myhq = building_at(s, hq_region_of(M, me));
  const Building *ohq = building_at(s, hq_region_of(M, opp));
  if (!myhq) return -1e9 + turn; // 내 HQ 파괴 = 패
  if (!ohq) return 1e9 - turn;   // 적 HQ 파괴 = 승 (빠를수록 좋음)
  double sc = 0;
  sc += 60.0 * (myhq->hp - ohq->hp);
  sc += 15.0 * ((ohq->max_hp_now() - ohq->hp) - (myhq->max_hp_now() - myhq->hp));
  sc += 40.0 * (myhq->level - ohq->level); // HQ 래더(판정·생산)
  double myhp = 0, ohp = 0;
  for (const auto &w : s.warriors) {
    if (w.hp <= 0) continue;
    (w.id.side == me ? myhp : ohp) += w.hp;
  }
  sc += 4.0 * (myhp - ohp);
  sc += 3.0 * (double)(side_income(s, me) - side_income(s, opp)); // 경제전
  int R = std::max(0, MAX_TURN - turn);
  sc += 0.05 * (double)(s.gold[(int)me] - s.gold[(int)opp]) * R / MAX_TURN; // 만기 골드
  return sc;
}

// Policy = 자유 수(경제/생산/공세 타이밍)의 노브. 값은 레거시 07031646r 프리셋 이식
// (30회 튜닝된 참고값). 강제 수(수비/끝내기)는 policy-무관.
struct Policy {
  int attack_mass;   // 공세 커밋 전 최소 집결 병력
  int econ_reserve;  // 업글이 이동/훈련 굶기지 않게 남길 골드
  int train_reserve; // 훈련 후 남길 골드
  int attack_turn;   // 경제 얇아도 이 턴까진 밀기
  int late_game;     // 이 턴 이후 판정 대비 HQ HP 은행
  int army_extra;    // 상비군 상한 = 총 work_cap + 이 값
  bool deep_hq;      // 중반에도 HQ L2 초과 업글(경제형)
};
namespace presets {
const Policy BALANCED{5, 200, 60, 40, 170, 6, false};
const Policy TURTLE{8, 350, 60, 90, 150, 10, true};
const Policy RUSH{3, 80, 0, 18, 175, 3, false};
const Policy EXPAND{6, 250, 60, 60, 165, 12, true};
const Policy BALANCED_MID{6, 140, 40, 35, 130, 10, true};
const Policy TURTLE_MID{9, 260, 50, 75, 120, 14, true};
const Policy RUSH_MID{4, 70, 0, 15, 150, 5, false};
const Policy EXPAND_MID{7, 170, 40, 50, 125, 16, true};
const Policy BALANCED_LARGE{5, 200, 100, 40, 170, 6, false};
const Policy TURTLE_LARGE{8, 350, 100, 90, 150, 10, true};
const Policy RUSH_LARGE{3, 80, 0, 18, 175, 3, false};
const Policy EXPAND_LARGE{6, 250, 100, 60, 165, 12, true};
} // namespace presets

static Cmds decide(const SimState &S, const GameMap &M, const Paths &P, int turn, BotState &bs,
                   const Policy &pol) {
  Cmds a;
  const Side me = M.my_side, opp = opposite(me);
  const int my_hq = M.my_hq, opp_hq = M.opp_hq, N = M.N;
  long long budget = S.gold[(int)me];
  const Building *hq = building_at(S, my_hq);
  const Building *ohq = building_at(S, opp_hq);
  const int my_whp = HQ_LEVELS[hq ? hq->level : 1].warrior_hp;
  const int train_cap = HQ_LEVELS[hq ? hq->level : 1].train_cap;

  std::set<int> lab;
  std::vector<const Warrior *> mobile = partition_units(S, M, lab);
  std::vector<const Warrior *> laborers;
  for (const auto &w : S.warriors)
    if (w.id.side == me && w.hp > 0 && lab.count(w.id.num)) laborers.push_back(&w);
  std::vector<const Building *> my_b, opp_b;
  for (const auto &b : S.buildings) (b.side == me ? my_b : opp_b).push_back(&b);
  std::set<int> used;

  auto issue_move = [&](const Warrior *w, int dest) -> bool {
    if (!w || used.count(w->id.num)) return false;
    if (w->region == dest || !reachable(P, w->region, dest)) return false;
    const Building *db = building_at(S, dest);
    int cost = (db && db->side == me) ? 0 : MOVE_COST;
    if (budget < cost) return false;
    budget -= cost;
    a.moves.push_back({w->id.num, dest});
    used.insert(w->id.num);
    return true;
  };
  auto avail = [&](const std::vector<const Warrior *> &pool) {
    std::vector<const Warrior *> v;
    for (auto w : pool) if (!used.count(w->id.num)) v.push_back(w);
    return v;
  };
  auto garrison_at = [&](int r) {
    int c = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == r) c++;
    return c;
  };
  auto on_my_side = [&](int r) {
    long long dm = reachable(P, my_hq, r) ? P.dist[my_hq][r] : PINF;
    long long doo = reachable(P, opp_hq, r) ? P.dist[opp_hq][r] : PINF;
    return dm < doo;
  };
  // Threat crossed on the direct path A->dest (intermediate regions only).
  auto path_threat = [&](int A, int dest) {
    if (!reachable(P, A, dest)) return 999;
    int cur = A, sum = 0, g = 0;
    while (cur != dest && g++ < N + 2) {
      int nx = P.nxt[cur][dest];
      if (nx < 0) break;
      if (nx != dest) {
        int t = 0;
        for (const auto &w : S.warriors) if (w.id.side == opp && w.hp > 0 && w.region == nx) t++;
        const Building *b = building_at(S, nx);
        if (b && b->side == opp) t += b->turret();
        sum += t;
      }
      cur = nx;
    }
    return sum;
  };
  // Safe offensive move: hold if the direct path crosses a lethal garrison and no
  // safe own-building waypoint makes progress (우회 라우팅, 축약판).
  auto issue_attack = [&](const Warrior *w, int dest) -> bool {
    if (!w || used.count(w->id.num) || !reachable(P, w->region, dest)) return false;
    if (path_threat(w->region, dest) <= 2) return issue_move(w, dest);
    int bestW = -1;
    long long bestD = P.dist[w->region][dest];
    for (const Building *b : my_b) {
      int W = b->region;
      if (W == w->region || W == dest) continue;
      if (!reachable(P, w->region, W) || !reachable(P, W, dest)) continue;
      if (P.dist[W][dest] >= bestD) continue;
      if (path_threat(w->region, W) > 2 || path_threat(W, dest) >= path_threat(w->region, dest)) continue;
      bestW = W;
    }
    if (bestW >= 0) return issue_move(w, bestW);
    if (path_threat(w->region, dest) >= 8) return false; // lethal, no route -> hold
    return issue_move(w, dest);
  };

  // === [토대] 경로 예측: nxt 역산 + Rule169 교집합 narrowing =====================
  std::vector<int> poi;
  for (auto b : my_b) poi.push_back(b->region);
  for (int sh : M.strongholds) poi.push_back(sh);
  std::map<int, int> cur_opp;
  for (const auto &w : S.warriors)
    if (w.id.side == opp && w.hp > 0) cur_opp[w.id.num] = w.region;
  std::map<int, std::set<int>> npred;
  for (auto &kv : cur_opp) {
    int sfx = kv.first, cur = kv.second;
    auto pit = bs.prev_opp.find(sfx);
    if (pit == bs.prev_opp.end()) continue; // new spawn: no motion yet
    int prev = pit->second;
    if (prev == cur) { // 정지 = 이동 완료(referee: 이동 중 재명령 불가) = 의도 해소.
      // 여정 중 후보를 동결하면 자기 요새에 앉은 노동자가 영구 유령 위협이 됨
      // (초반 한 마리 확장을 공격으로 오인 -> 수비 낭비). 후보 = {현 위치}로 붕괴:
      // 내 건물 위면 실제 공성으로 계속 위협, 아니면 무위협. 재이동 시 새 후보 생성.
      npred[sfx] = {cur};
      continue;
    }
    std::set<int> cons; // targets whose shortest first step matches the observed move
    for (int B : poi)
      if (B != prev && reachable(P, prev, B) && P.nxt[prev][B] == cur) cons.insert(B);
    auto q = bs.pred.find(sfx);
    if (q == bs.pred.end() || q->second.empty()) npred[sfx] = cons;
    else {
      std::set<int> in;
      for (int x : q->second) if (cons.count(x)) in.insert(x);
      npred[sfx] = in.empty() ? cons : in; // empty intersection => target changed, reset
    }
  }
  bs.pred = npred;
  bs.prev_opp = cur_opp;

  // === 공격 판별 + 위협 할당 (예측 목적지 role; frontier 통과 폴백) ==============
  std::map<int, std::vector<int>> atk_hps; // my region -> incoming enemy hps
  std::map<int, long long> wave_eta;
  auto is_mine_bldg = [&](int r) { const Building *b = building_at(S, r); return b && b->side == me; };
  for (const auto &w : S.warriors) {
    if (w.id.side != opp || w.hp <= 0) continue;
    int tgt = -1;
    long long bd = PINF;
    auto pit = bs.pred.find(w.id.num);
    if (pit != bs.pred.end())
      for (int B : pit->second)
        if (is_mine_bldg(B) && reachable(P, w.region, B)) {
          long long d = P.dist[w.region][B];
          if (d < bd) { bd = d; tgt = B; }
        }
    // "전진 중" 게이트 (레거시): 아직 자기 HQ에 더 가까우면 위협 아님. 턴1 상대 첫
    // 이동(HQ->인접)은 확장이든 공격이든 첫 스텝이 같아 모호 -> 내 HQ 공격 오판 +
    // 불필요 비상훈련(2번째 기지 밀림). 중간선을 넘어와야(전진) 위협으로 계상.
    if (tgt >= 0) {
      long long d_home = reachable(P, w.region, opp_hq) ? P.dist[w.region][opp_hq] : PINF;
      if (bd > d_home) tgt = -1;
    }
    // frontier 폴백: "이동 중"인 유닛만 침입 의도로 간주. 정지 유닛은 의도 해소
    // (내 쪽에 앉은 상대 기지 노동자를 영구 공격자로 오인하는 유령 차단).
    if (tgt < 0 && w.state == WState::MOVING && on_my_side(w.region))
      for (auto b : my_b) {
        if (!reachable(P, w.region, b->region)) continue;
        long long d = P.dist[w.region][b->region];
        if (d < bd) { bd = d; tgt = b->region; }
      }
    if (tgt < 0) continue;
    atk_hps[tgt].push_back(w.hp);
    long long e = turns_to(P, N, w.region, tgt);
    auto it = wave_eta.find(tgt);
    if (it == wave_eta.end() || e < it->second) wave_eta[tgt] = e;
  }

  // === [강제] 수비: grind-window need-bound. HQ 생산우선, 못지킬 기지 포기 ========
  auto draft_to = [&](int r, long long window, int need, int &have,
                      std::vector<const Warrior *> &pool) {
    while (have < need) {
      const Warrior *pick = nullptr;
      long long best = PINF;
      for (auto w : avail(pool)) {
        if (w->region == r) { pick = w; best = -1; break; }
        if (!reachable(P, w->region, r)) continue;
        long long t = turns_to(P, N, w->region, r);
        if (t > window) continue;
        if (t < best) { best = t; pick = w; }
      }
      if (!pick) break;
      if (pick->region == r) { used.insert(pick->id.num); have++; }
      else if (issue_move(pick, r)) have++;
      else break;
    }
  };
  bool hq_threatened = false;
  int hq_need = 0, hq_have = 0;
  std::vector<int> def_order;
  if (atk_hps.count(my_hq)) def_order.push_back(my_hq);
  for (auto &kv : atk_hps) if (kv.first != my_hq) def_order.push_back(kv.first);
  for (int r : def_order) {
    const Building *b = building_at(S, r);
    if (!b) continue;
    int turret = b->turret();
    int need = min_defenders_to_hold(atk_hps[r], my_whp, turret, 64);
    if (r == my_hq && need <= 62 && !(M.N >= 70 && M.N < 100)) need += 2;
    if (need > 64 && r != my_hq) continue; // 못지킬 기지: 헛소모 방지, 포기
    int incoming = (int)atk_hps[r].size();
    long long eta = wave_eta.count(r) ? wave_eta[r] : 0;
    long long grind = std::max(1, b->hp) / std::max(1, incoming - turret);
    long long window = eta + std::max(1LL, grind);
    int have = garrison_at(r);
    if (r == my_hq) {
      // 생산 우선(window 0, 온사이트 즉턴), 부족분만 잉여->광부 징집
      int shortfall = need - have;
      if (hq && shortfall > 0 && budget >= TRAIN_COST) {
        int n = (int)std::min<long long>({(long long)shortfall, (long long)train_cap, budget / TRAIN_COST});
        if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; have += n; }
      }
      draft_to(r, window, need, have, mobile);
      draft_to(r, window, need, have, laborers);
      hq_threatened = true; hq_need = need; hq_have = have;
    } else {
      draft_to(r, window, need, have, mobile); // 기지: 훈련 불가 -> 징집만
    }
  }

  // === [자유·경제] 레거시 07031646r 빌드오더 이식 =============================
  auto is_sh = [&](int r) { return std::binary_search(M.strongholds.begin(), M.strongholds.end(), r); };
  long long lane = reachable(P, my_hq, opp_hq) ? P.dist[my_hq][opp_hq] : PINF;
  int hq_turret = hq ? hq->turret() : 0;
  int army_ct = 0, opp_ct = 0, total_cap = 0;
  for (const auto &w : S.warriors) (w.id.side == me ? army_ct : opp_ct)++;
  for (auto b : my_b) total_cap += b->work_cap();
  int incoming_hq = atk_hps.count(my_hq) ? (int)atk_hps[my_hq].size() : 0;
  bool rush = incoming_hq >= std::max(3, garrison_at(my_hq) + hq_turret);
  bool behind = opp_ct > army_ct || (int)opp_b.size() > (int)my_b.size();
  // 상대 잉여(야전) 병력에 유동 대응 (사용자 요청): 자기 건물에 정주하지 않은 상대
  // 유닛 = 공세 잠재력(노동자 제외). 그중 아직 자기 쪽에 정지해 쌓인 것 = 올인 집결
  // 신호(레거시 staged_hq_mass 개념). 내 야전군보다 크게 집결 중이면 brace.
  int opp_field = 0, opp_massed = 0;
  int my_field = (int)mobile.size();
  for (const auto &w : S.warriors) {
    if (w.id.side != opp || w.hp <= 0) continue;
    const Building *wb = building_at(S, w.region);
    if (wb && wb->side == opp && w.state == WState::STATIONARY) continue; // 상대 노동자/주둔
    opp_field++;
    bool advancing = reachable(P, w.region, my_hq) && reachable(P, w.region, opp_hq) &&
                     P.dist[w.region][my_hq] < P.dist[w.region][opp_hq];
    if (!advancing && w.state == WState::STATIONARY) opp_massed++;
  }
  bool opp_bracing = opp_massed >= my_field + 3 && opp_massed >= 4; // 집결 = 선제 대응
  bs.dbg_thr = (int)atk_hps.size(); bs.dbg_att = 0;
  for (auto &kv : atk_hps) bs.dbg_att += (int)kv.second.size();
  bs.dbg_inhq = incoming_hq; bs.dbg_rush = rush ? 1 : 0;

  // EconomyManager: 정착민이 서 있는 빈 요새에 기지 건설 (거점당 1개만 — 중복 UPGRADE=WA)
  std::set<int> built_here;
  for (auto w : avail(mobile)) {
    int r = w->region;
    if (!is_sh(r) || building_at(S, r) || enemy_warrior_at(S, r, me) || built_here.count(r)) continue;
    if (budget < BASE_BUILD_COST) break;
    a.upgrades.push_back(r);
    built_here.insert(r);
    budget -= BASE_BUILD_COST;
    used.insert(w->id.num);
  }
  // === HQ 래더 + hp-타이브레이크 fund (레거시 07031347d C4/C5·J3·R4·R12 이식) =====
  int opp_hq_lvl = (ohq && ohq->side == opp) ? ohq->level : 0;
  // C4: 경제 우위(수입 20:13 & 골드리드>600)면 L3까지 선제 업글(타이브레이크 보험).
  bool income_dom = (long long)side_income(S, me) * 13 >= (long long)side_income(S, opp) * 20 &&
                    (S.gold[(int)me] - S.gold[(int)opp]) > 600;
  // J3: 기지·병력 둘 다 뒤지면 업글=수비 붕괴 -> 스킵(병력 우선). 단 late 창은 예외(R4).
  bool deep_behind = (int)opp_b.size() > (int)my_b.size() && opp_ct > army_ct;
  bool ladder_window = turn >= pol.late_game;
  bool did_hq = false;
  if (hq && !hq_threatened && garrison_at(my_hq) >= 1 && !enemy_warrior_at(S, my_hq, me) &&
      hq->level < HQ_MAX_LEVEL) {
    // HQ 레벨을 상대 이상으로: train_cap(생산)·work_cap(경제)·turret+maxHP(수비·판정).
    bool want = (hq->level == 1 || turn >= pol.late_game || (pol.deep_hq && hq->level < 4) ||
                 (opp_hq_lvl >= hq->level) || (income_dom && hq->level < 3)) &&
                (!deep_behind || ladder_window);
    int cost = HQ_LEVELS[hq->level + 1].upgrade_cost;
    if (want && budget >= cost + pol.econ_reserve) { a.upgrades.push_back(my_hq); budget -= cost; did_hq = true; }
  }
  // R12: 잉여 트레이너가 매턴 골드를 idle까지 태워 업글 문턱에 영영 못 닿음. 업글이
  // 타이브레이크 NEED(opp HQ >= 내 HQ, income_dom, 또는 late 창)면 그 SURPLUS 트레이너
  // + 기지 렙업에서만 업글 비용을 예약(hq_fund). parity/비상/정규 훈련은 불가침(병력
  // 안전 우선) -> 페이싱 무변. tie(>=)에서도 예약해야 다음 레벨 선점 = 판정승.
  // MY-bot 조정: 병력에서 뒤질 땐 fund 미발동(레거시는 항상 발동 -> 내 봇에선 상대
  // 래더 추격에 잉여를 계속 뺏겨 병력 열세 심화 -> 42%. army 우위/대등일 때만 뱅크).
  bool army_ok = army_ct + ((M.N >= 70 && M.N < 100) ? 2 : 4) >= opp_ct;
  long long hq_fund = 0;
  if (hq && hq->level < HQ_MAX_LEVEL && (!deep_behind || ladder_window) && !did_hq && army_ok &&
      (opp_hq_lvl >= hq->level || (income_dom && hq->level < 3) || turn >= pol.late_game))
    hq_fund = HQ_LEVELS[hq->level + 1].upgrade_cost;
  // 확장: 동시에 한 정착민만 (inflight). 지는 레이스는 스킵(SETTLER_RACE_MARGIN=2).
  int inflight = 0;
  for (const auto &w : S.warriors) {
    if (w.id.side != me || w.hp <= 0) continue;
    int loc = (w.state == WState::MOVING) ? w.target : w.region;
    if (is_sh(loc) && !building_at(S, loc)) inflight++;
  }
  bool rusher_hold = rush && (int)my_b.size() >= 2; // 러쉬 중엔 HQ+2에서 확장 멈춤
  if (inflight == 0 && !rusher_hold) {
    const long long RACE = 2;
    std::vector<int> order;
    for (int sh : M.strongholds) if (reachable(P, my_hq, sh)) order.push_back(sh);
    std::sort(order.begin(), order.end(), [&](int x, int y) { return P.dist[my_hq][x] < P.dist[my_hq][y]; });
    for (int sh : order) {
      if (building_at(S, sh) || enemy_warrior_at(S, sh, me)) continue;
      const Warrior *w = nullptr;
      long long best = PINF;
      for (auto m2 : avail(mobile)) {
        if (!reachable(P, m2->region, sh)) continue;
        long long d = P.dist[m2->region][sh];
        if (d < best) { best = d; w = m2; }
      }
      if (!w) continue;
      // 레이스 판정: "실제로 sh를 노리는" 상대만 레이서 (유령 위협과 동형 오류 방지 —
      // 모든 유휴 상대를 레이서로 보면 중앙 요새 전부 헌납, 확장 3기지 고착).
      // 레이서 = (a) sh 위/인접에 선 유닛, (b) MOVING이고 예측 후보에 sh 포함.
      long long enemy_eta = PINF;
      for (const auto &w2 : S.warriors) {
        if (w2.id.side == me || w2.hp <= 0) continue;
        bool racer = false;
        if (w2.state == WState::MOVING) {
          auto pit2 = bs.pred.find(w2.id.num);
          racer = (pit2 == bs.pred.end()) || pit2->second.empty() || pit2->second.count(sh);
        } else {
          racer = (w2.region == sh);
          if (!racer)
            for (int nb : M.adj[sh]) if (w2.region == nb) { racer = true; break; }
        }
        if (!racer) continue;
        int loc2 = (w2.state == WState::MOVING) ? w2.target : w2.region;
        if (reachable(P, loc2, sh)) enemy_eta = std::min(enemy_eta, P.dist[loc2][sh]);
      }
      if (enemy_eta + RACE <= P.dist[w->region][sh]) continue; // clearly lost race
      issue_move(w, sh);
      break; // 한 정착민만
    }
  }
  // MilitaryManager: parity-paced 상비군 (econ_open 게이트, 적 도착까지 페이싱)
  if (hq && a.train_n == 0) {
    int cap = HQ_LEVELS[hq->level].train_cap;
    const int ARMY_HARD_MAX = 60;
    long long t_arr = wave_eta.count(my_hq) ? wave_eta[my_hq]
                      : (lane < PINF ? turns_to(P, N, my_hq, opp_hq) : PINF);
    int parity_buf = std::max(2, 4 - hq_turret);
    int parity = std::min(opp_ct + parity_buf, ARMY_HARD_MAX);
    int econ_floor = std::max(3, total_cap + pol.army_extra);
    int needed_now = (t_arr >= PINF) ? econ_floor
                     : rush ? parity
                            : parity - cap * (int)std::min<long long>(t_arr, parity);
    if (behind) needed_now = std::max(needed_now, std::min(opp_ct, ARMY_HARD_MAX) - 2);
    // 상대 집결(brace)엔 도착-페이싱을 접고 야전군을 즉시 매칭(선제 수비 준비).
    if (opp_bracing) needed_now = std::max(needed_now, std::min(opp_field, ARMY_HARD_MAX));
    int cap_army = std::max(econ_floor, needed_now);
    int income_after = WORK_INCOME * std::min(total_cap, army_ct + 1);
    bool sustainable = income_after - UPKEEP_PER_WARRIOR * (army_ct + 1) >= 0;
    bool room = army_ct < cap_army;
    long long floor = pol.train_reserve + (inflight > 0 ? BASE_BUILD_COST : 0); // parity 불가침
    // 경제-우선 오프닝(레거시 07031347d L1874 그대로): HQ+2 기지 전엔 상비군 0.
    // 러쉬(상대 공세)일 때만 HQ+1로 앞당김. 맵크기 조정은 제거 — 내가 넣었던
    // "N<=63 -> open_bases=2"가 작은 맵에서 2번째 기지 전에 병력을 뽑아 econ 템포를
    // 깨뜨리는 근인(사용자 관측: 2기지 전 꾸준한 유닛생산 = 지속 손해). 레거시는
    // 작은 맵에서도 econ-first 유지(seed7: 상대 base2 @T8). 비선형이므로 맵크기
    // 단조조정은 위험 — 상대 성향(러쉬)에만 반응.
    size_t open_bases = rush ? 2 : 3;
    // econ_open은 "기지 뒤짐"으로 깨지 말 것(전진 게이트와 동형): 조기 개방은 실제
    // 위협(상대 병력 우위)일 때만. 기지 격차는 확장으로 해소.
    bool econ_open = my_b.size() >= open_bases || hq_threatened || rush || opp_ct > army_ct || opp_bracing;
    if (room && sustainable && econ_open && budget >= TRAIN_COST + floor) {
      int n = (int)std::min<long long>(cap, (budget - floor) / TRAIN_COST);
      n = std::min(n, cap_army - army_ct);
      if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; }
    }
    // idle-gold: 안전+골드 잉여면 상한 넘어 병력 전환 (레거시 E; 후반 덜 쟁임)
    if (a.train_n == 0 && !hq_threatened && incoming_hq == 0 && army_ct < ARMY_HARD_MAX && econ_open) {
      long long idle = 200 + 400LL * std::max(0, MAX_TURN - turn) / MAX_TURN + hq_fund; // 잉여는 HQ 래더 우선
      if (budget >= TRAIN_COST + idle) {
        int n = (int)std::min<long long>(cap, (budget - idle) / TRAIN_COST);
        n = std::min(n, ARMY_HARD_MAX - army_ct);
        if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; }
      }
    }
  }
  // 기지 레벨업 (army 뒤, 채울 노동자 있을 때만; work_cap 확장)
  if (!hq_threatened)
    for (const Building *b : my_b) {
      if (b->type != BType::BASE || b->level >= BASE_MAX_LEVEL) continue;
      if (garrison_at(b->region) < b->work_cap()) continue;
      int cost = BASE_LEVELS[b->level + 1].cost;
      // C5: 기지 렙업이 HQ fund를 새게 하지 말 것(레거시 t168 base-L3 drain 회귀).
      if (budget >= cost + pol.econ_reserve + 200 + hq_fund) { a.upgrades.push_back(b->region); budget -= cost; break; }
    }

  // === [강제] 끝내기 + 타깃선정(가치/비용) + anti-trickle (잉여 army) ===========
  std::vector<const Warrior *> army = avail(mobile);
  int attack_force = (int)army.size();
  std::vector<int> ohq_def;
  for (const auto &w : S.warriors)
    if (w.id.side == opp && w.hp > 0 && w.region == opp_hq) ohq_def.push_back(w.hp);
  int ohq_turret = (ohq && ohq->side == opp) ? ohq->turret() : 0;
  int ohq_tc = (ohq && ohq->side == opp) ? HQ_LEVELS[ohq->level].train_cap : 0; // R11 retrain
  int ohq_whp = HQ_LEVELS[(ohq && ohq->side == opp) ? ohq->level : 1].warrior_hp;
  int ohq_sustain = ohq_tc >= 2 ? std::min<int>(12, (int)(10 * side_income(S, opp) / TRAIN_COST)) : 0;
  // 행군 중 재훈련 반영: 먼 HQ일수록 도착 전에 상대가 수비를 쌓음(포렌식 70074_5
  // T94: 12마리를 먼 HQ(10주둔)에 커밋 -> 단발 전멸, 기지를 쳤어야). 도착 시점(웨이브
  // max ETA)까지 opp가 tc/턴으로 뽑는 수(골드 상한) 만큼 방어를 미리 더해 커밋을
  // 보수화 -> 미달이면 hq_finishable=false -> 기지 공격으로 폴백.
  long long hq_eta = 0;
  for (auto w : army)
    if (reachable(P, w->region, opp_hq)) hq_eta = std::max(hq_eta, P.dist[w->region][opp_hq]);
  int march_reinf = (ohq_tc > 0)
      ? (int)std::min<long long>({(long long)ohq_tc * hq_eta,
                                  12LL * side_income(S, opp) / TRAIN_COST, 24LL})
      : 0;
  for (int i = 0; i < march_reinf; ++i) ohq_def.push_back(ohq_whp);
  int hq_clear = min_mass_to_take(my_whp, ohq_def, ohq_turret, 64, ohq_whp, ohq_tc, ohq_sustain);
  int target = -1, need_mass = 0;
  // HQ 커밋은 보수적으로: 행군 중 재훈련이 방어를 부풀리므로 큰 마진 + 상대 파산일 때만.
  // (공격 자살 방지 — 잉여 army는 그 외엔 홈 대기 = 수비·판정 예비.)
  bool opp_broke = side_income(S, opp) < 120;
  int hq_commit = hq_clear + ((M.N >= 70 && M.N < 100) ? std::max(4, hq_clear / 2) : std::max(3, hq_clear / 3));
  bool hq_finishable = ohq && ohq->side == opp && hq_clear <= 64 &&
                       attack_force >= hq_commit && (opp_broke || attack_force >= hq_clear + ((M.N >= 70 && M.N < 100) ? 8 : 6));
  if (hq_finishable) {
    target = opp_hq; need_mass = hq_clear;
  } else {
    long long best_score = LLONG_MIN;
    for (const Building *b : opp_b) {
      if (b->type != BType::BASE || !reachable(P, my_hq, b->region)) continue;
      // 수비 = 현 주둔 + "내 도착+2턴 내 닿는" 적 증원. "기지=정적 수비" 가정은
      // 작은 맵에서 붕괴(상대 HQ 3홉이면 need 3 웨이브가 실제 3+4를 만나 전멸
      // 반복 = N<=63 전패의 근인). 창 내 상대 HQ면 재훈련 스트림도 리졸버 반영.
      long long my_eta = P.dist[my_hq][b->region]; // 주력=HQ권 근사
      std::vector<int> def;
      for (const auto &w : S.warriors) {
        if (w.id.side != opp || w.hp <= 0) continue;
        if (w.region == b->region ||
            (reachable(P, w.region, b->region) && P.dist[w.region][b->region] <= my_eta + 2))
          def.push_back(w.hp);
      }
      bool hq_in_win = reachable(P, opp_hq, b->region) && P.dist[opp_hq][b->region] <= my_eta + 2;
      int win_sustain = hq_in_win ? std::min(4, (int)(4 * side_income(S, opp) / TRAIN_COST)) : 0;
      int need = min_mass_to_take(my_whp, def, b->turret(), 64, ohq_whp,
                                  hq_in_win ? ohq_tc : 0, win_sustain);
      if (need > 64) continue;
      long long d = P.dist[my_hq][b->region];
      // JIT 안전마진(사용자 요청 70074_8): 상대 증원(opp_hq->t)이 내 도착보다 늦을수록
      // 안전히 치고 복귀·다음 공격 셋업 가능 -> 그런 표적을 선호. (전방 이동은 HQ
      // 노출로 실측 붕괴 -> 이동 없이 "선택"만 안전쪽으로.)
      long long opp_reinf = reachable(P, opp_hq, b->region) ? P.dist[opp_hq][b->region] : (d + 30);
      long long safety = std::max<long long>(-6, std::min<long long>(opp_reinf - my_eta, 12));
      long long score = 80LL * b->work_cap() - 3 * d - 20LL * (int)def.size() - 10LL * b->turret() +
                        6LL * safety;
      if (score > best_score) { best_score = score; target = b->region; need_mass = std::max(need, 2); }
    }
  }
  // --- wave commitment: kill the trickle (design: 랠리 폐지·분산 동시수렴) ---------
  // A wave already in flight to a committed target is NOT reinforced one unit at a
  // time — single-file arrivals just feed the turret (warriors never heal). New army
  // holds at home as a defense/finish reserve until the committed wave RESOLVES
  // (target captured/destroyed, or the wave is spent); only then do we gather and
  // launch a fresh synchronized wave. This also stabilizes the target across turns.
  bool wave_in_flight = false;
  if (bs.atk_target >= 0) {
    const Building *tb = building_at(S, bs.atk_target);
    bool tgt_live = tb && tb->side == opp; // not yet captured/destroyed
    int committed = 0;
    for (const auto &w : S.warriors) {
      if (w.id.side != me || w.hp <= 0) continue;
      int loc = (w.state == WState::MOVING) ? w.target : w.region;
      if (loc == bs.atk_target) committed++;
    }
    if (tgt_live && committed > 0) wave_in_flight = true;
    else bs.atk_target = -1; // resolved -> free to re-commit
  }
  // 전원 즉시 발사(주력이 HQ 스폰 공유로 대체로 동위치). eta-스케줄 출발(분산 대기
  // 후 동시수렴)은 실측 악화(4-5-1 -> 3-7-0): 대기 유닛이 수비에 재차출돼 웨이브가
  // 새고, 출발창을 놓친 유닛이 영구 이탈 -> 미달 전력 도착. 즉시 발사가 강건.
  // HQ 위협 중엔 기지 공격으로 새 웨이브를 내보내지 않음 — 원정 나가면 소수 수비만
  // 남아 러쉬에 HQ 함락(포렌식 70074_3: 병력 2배인데 원정 SIEGE 중 레이스 패). 단
  // 상대 HQ 끝내기(승리 레이스)는 예외로 허용.
  bool may_launch = !hq_threatened || target == opp_hq;
  if (target >= 0 && need_mass <= 64 && !wave_in_flight && attack_force >= need_mass && may_launch) {
    for (auto w : army) issue_attack(w, target);
    bs.atk_target = target;
  }
  // 안전망: 중복 UPGRADE 제거 (referee가 같은 구역 중복 명령을 WA 처리)
  {
    std::set<int> seen;
    std::vector<int> u;
    for (int r : a.upgrades) if (seen.insert(r).second) u.push_back(r);
    a.upgrades.swap(u);
  }
  return a;
}

// ===================== StrategyManager: 잉여배분 롤아웃 (Phase 3) =============
// 자유 수(Policy)를 K턴 롤아웃(정확 시뮬 재사용)해 value function으로 채점 -> 최선 선택.
// 강제 수(수비/끝내기)는 policy-무관하게 decide 안에서 결정 -> 롤아웃은 잉여배분만 탐색.
// 상대는 고정 BALANCED(우리 복사본=미러 아님) -> 무승부 편향·맹점공유 회피(설계 디테일1 MAP판).
static Cmds strategy_manager(const SimState &S0, const GameMap &M, const Paths &P, int turn,
                             BotState &bs) {
  const Side me = M.my_side, opp = opposite(me);
  const int K = std::max(1, std::min(MAX_TURN - turn, (M.N >= 70 && M.N < 100) ? 8 : 10)); // credible horizon
  const bool mid_map = M.N >= 70 && M.N < 100;
  const bool large_map = M.N >= 100;
  const Policy cands[] = {
      mid_map ? presets::BALANCED_MID : (large_map ? presets::BALANCED_LARGE : presets::BALANCED),
      mid_map ? presets::TURTLE_MID : (large_map ? presets::TURTLE_LARGE : presets::TURTLE),
      mid_map ? presets::RUSH_MID : (large_map ? presets::RUSH_LARGE : presets::RUSH),
      mid_map ? presets::EXPAND_MID : (large_map ? presets::EXPAND_LARGE : presets::EXPAND)
  };
  const Policy oppol = mid_map ? presets::BALANCED_MID : (large_map ? presets::BALANCED_LARGE : presets::BALANCED);
  GameMap oppM = M;
  oppM.my_side = opp;
  oppM.my_hq = hq_region_of(M, opp);
  oppM.opp_hq = hq_region_of(M, me);
  double best = -1e18;
  const Policy *bestP = &cands[0];
  // 시간 예산: 롤아웃(64 decide+step)은 유닛수²로 커져 후반 실측 148ms/턴 -> 뱅크
  // 소진 -> TLE 몰수(실전 batch69602 game5 T163). 벽시계로 컷 -> 항상 <100ms.
  // 뱅크가 얕으면 더 보수적. 정책 사이 + ply 사이 둘 다 체크(최악 오버슛=1 ply).
  const double budget = g_tokens_left <= 1 ? 30.0 : 45.0;
  for (const Policy &pol : cands) {
    if (elapsed_ms() > budget) break;
    SimState s = S0;
    for (int d = 0; d < K; ++d) {
      if (elapsed_ms() > budget) break;
      BotState dme, dopp; // rollout: no prediction tracker (cheap approx, frontier defense)
      Cmds cme = decide(s, M, P, turn + d, dme, pol);
      Cmds copp = decide(s, oppM, P, turn + d, dopp, oppol);
      Cmds &cl = (me == Side::LEFT) ? cme : copp;
      Cmds &cr = (me == Side::LEFT) ? copp : cme;
      step(s, M, P, cl, cr);
      if (!building_at(s, hq_region_of(M, me)) || !building_at(s, hq_region_of(M, opp))) break;
    }
    double e = eval_state(s, M, me, turn + K);
    if (e > best) { best = e; bestP = &pol; }
  }
  return decide(S0, M, P, turn, bs, *bestP); // 실턴: live 예측 + 최선 policy
}

// ===================== live main (clean-room) ================================
int main() {
  GameMap M;
  SimState S;
  parse_init(M, S);
  Paths P = calculate_paths(M);
  BotState bs;
  int turn;
  while (read_turn_start(turn)) {
    g_turn_start = std::chrono::steady_clock::now(); // 턴 시간예산 시작
    Cmds a = strategy_manager(S, M, P, turn, bs);
    emit_cmds(M, a);
    read_turn_result(S, M, a);
#ifdef BOTDBG
    {
      std::ofstream fp("botdbg.txt", std::ios::app);
      int ar = 0, op = 0, mb = 0, ob = 0, ml = 1, ol = 1;
      for (const auto &w : S.warriors) (w.id.side == M.my_side ? ar : op)++;
      for (const auto &b : S.buildings) (b.side == M.my_side ? mb : ob)++;
      if (const Building *h = building_at(S, M.my_hq)) ml = h->level;
      if (const Building *h = building_at(S, M.opp_hq)) ol = h->level;
      int hqgar = 0, near_enemy = 0, hqhp = 0;
      long long lane = reachable(P, M.my_hq, M.opp_hq) ? P.dist[M.my_hq][M.opp_hq] : 1;
      for (const auto &w : S.warriors) {
        if (w.hp <= 0) continue;
        if (w.id.side == M.my_side && w.region == M.my_hq) hqgar++;
        if (w.id.side != M.my_side && reachable(P, w.region, M.my_hq) &&
            P.dist[w.region][M.my_hq] < lane / 2)
          near_enemy++;
      }
      if (const Building *h = building_at(S, M.my_hq)) hqhp = h->hp;
      fp << "T" << turn << " gold=" << S.gold[(int)M.my_side] << " army=" << ar
         << " oarmy=" << op << " mb=" << mb << " ob=" << ob << " hq=" << ml
         << " ohq=" << ol << " hqhp=" << hqhp << " hqgar=" << hqgar
         << " nearHQ=" << near_enemy << " train=" << a.train_n
         << " moves=" << a.moves.size() << " ups=" << a.upgrades.size()
         << " thr=" << bs.dbg_thr << " att=" << bs.dbg_att
         << " inHQ=" << bs.dbg_inhq << " rush=" << bs.dbg_rush << "\n";
    }
#endif
  }
  return 0;
}
// ========================= SIM_TEST / OBS_TEST gate ==========================
#else

static std::vector<std::string> split(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  for (std::string t; is >> t;) out.push_back(t);
  return out;
}

// Replay a referee log, asserting step() reproduces each turn's result block.
static int replay(const std::string &path) {
  std::ifstream in(path);
  if (!in) {
    std::cerr << "cannot open " << path << "\n";
    return 1;
  }
  std::vector<std::string> L;
  for (std::string s; std::getline(in, s);) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    L.push_back(s);
  }
  size_t i = 0;
  while (i < L.size() && L[i] != "MAP") i++;
  if (i >= L.size()) { std::cerr << "no MAP block\n"; return 1; }
  i++; // skip "MAP"
  GameMap M;
  {
    auto t = split(L[i++]);
    M.N = std::stoi(t[0]);
    M.K = std::stoi(t[1]);
  }
  { auto t = split(L[i++]); for (int j = 0; j < M.N; ++j) M.x.push_back(std::stoll(t[j])); }
  { auto t = split(L[i++]); for (int j = 0; j < M.N; ++j) M.y.push_back(std::stoll(t[j])); }
  { auto t = split(L[i++]); for (size_t j = 1; j < t.size(); ++j) M.strongholds.push_back(std::stoi(t[j])); }
  std::sort(M.strongholds.begin(), M.strongholds.end());
  M.adj.assign(M.N, {});
  for (int r = 0; r < M.N; ++r) {
    auto t = split(L[i++]);
    int deg = std::stoi(t[0]);
    for (int j = 0; j < deg; ++j) M.adj[r].push_back(std::stoi(t[1 + j]));
    std::sort(M.adj[r].begin(), M.adj[r].end());
  }
  if (i < L.size() && L[i] == "END MAP") i++;

  Paths P = calculate_paths(M);
  SimState st = init_sim(M);

  auto parse_block = [&](size_t &k, Side s) -> Cmds {
    Cmds c;
    // expect "COMMAND <SIDE> START"
    k++; // skip COMMAND ... START
    while (k < L.size() && L[k].rfind("COMMAND", 0) != 0) {
      auto t = split(L[k]);
      if (!t.empty()) {
        if (t[0] == "MOVE") c.moves.push_back({std::stoi(t[1].substr(1)), std::stoi(t[2])});
        else if (t[0] == "TRAIN") c.train_n = std::stoi(t[1]);
        else if (t[0] == "UPGRADE") c.upgrades.push_back(std::stoi(t[1]));
      }
      k++;
    }
    k++; // skip COMMAND ... END
    (void)s;
    return c;
  };

  int turns = 0, day = 0;
  while (i < L.size()) {
    if (L[i].rfind("RESULT", 0) == 0) break;
    if (L[i].rfind("TURN ", 0) != 0) { i++; continue; }
    // "TURN d"
    day = std::stoi(split(L[i])[1]);
    i++;
    Cmds cl = parse_block(i, Side::LEFT);
    Cmds cr = parse_block(i, Side::RIGHT);
    // "TURN d RESULT"
    if (i < L.size() && L[i].rfind("TURN ", 0) == 0) i++;
    // "TIME ..."
    if (i < L.size() && L[i].rfind("TIME", 0) == 0) i++;
    std::vector<std::string> expected;
    while (i < L.size() && L[i].rfind("END TURN", 0) != 0) {
      if (!L[i].empty()) expected.push_back(L[i]);
      i++;
    }
    if (i < L.size()) i++; // skip END TURN

    ResultBlock rb = step(st, M, P, cl, cr);
    std::vector<std::string> got = render_result(rb);
    if (got != expected) {
      std::cerr << "MISMATCH at TURN " << day << " in " << path << "\n";
      std::cerr << "--- expected (" << expected.size() << ") ---\n";
      for (auto &e : expected) std::cerr << e << "\n";
      std::cerr << "--- got (" << got.size() << ") ---\n";
      for (auto &g : got) std::cerr << g << "\n";
      return 1;
    }
    turns++;
  }
  std::cout << "OK " << path << " : " << turns << " turns matched\n";
  return 0;
}

#ifdef OBS_TEST
// Observable state = what both players can see: per-unit (side, suffix, region,
// hp) and per-building (region, side, type, level, hp). Move state/target is
// internal bookkeeping (unknowable for the opponent) and excluded.
static bool states_match(const SimState &a, const SimState &b) {
  auto wk = [](const SimState &s) {
    std::vector<std::tuple<int, int, int, int>> v;
    for (const auto &w : s.warriors)
      v.push_back({(int)w.id.side, w.id.num, w.region, w.hp});
    std::sort(v.begin(), v.end());
    return v;
  };
  auto bk = [](const SimState &s) {
    std::vector<std::tuple<int, int, int, int, int>> v;
    for (const auto &bl : s.buildings)
      v.push_back({bl.region, (int)bl.side, (int)bl.type, bl.level, bl.hp});
    std::sort(v.begin(), v.end());
    return v;
  };
  return wk(a) == wk(b) && bk(a) == bk(b);
}

static void dump_state(const char *tag, const SimState &s) {
  std::cerr << tag << " W:";
  for (const auto &w : s.warriors)
    std::cerr << " " << (w.id.side == Side::LEFT ? 'A' : 'B') << w.id.num << "@"
              << w.region << "h" << w.hp;
  std::cerr << " | B:";
  for (const auto &b : s.buildings)
    std::cerr << " r" << b.region << (b.side == Side::LEFT ? 'A' : 'B') << "L"
              << b.level << "h" << b.hp;
  std::cerr << "\n";
}

// Validate the LIVE world-model reconstruction (apply_result_block) against the
// authoritative state (step on exact commands). For each viewpoint side we feed
// the referee-format result block (derived from step's ResultBlock) into
// apply_result_block and assert the reconstructed observable state matches; opp
// gold is the only estimate, so we report its error instead of asserting.
static int replay_observed(const std::string &path) {
  std::ifstream in(path);
  if (!in) { std::cerr << "cannot open " << path << "\n"; return 1; }
  std::vector<std::string> L;
  for (std::string s; std::getline(in, s);) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    L.push_back(s);
  }
  size_t i = 0;
  while (i < L.size() && L[i] != "MAP") i++;
  if (i >= L.size()) { std::cerr << "no MAP block\n"; return 1; }
  i++;
  GameMap M;
  { auto t = split(L[i++]); M.N = std::stoi(t[0]); M.K = std::stoi(t[1]); }
  { auto t = split(L[i++]); for (int j = 0; j < M.N; ++j) M.x.push_back(std::stoll(t[j])); }
  { auto t = split(L[i++]); for (int j = 0; j < M.N; ++j) M.y.push_back(std::stoll(t[j])); }
  { auto t = split(L[i++]); for (size_t j = 1; j < t.size(); ++j) M.strongholds.push_back(std::stoi(t[j])); }
  std::sort(M.strongholds.begin(), M.strongholds.end());
  M.adj.assign(M.N, {});
  for (int r = 0; r < M.N; ++r) {
    auto t = split(L[i++]);
    int deg = std::stoi(t[0]);
    for (int j = 0; j < deg; ++j) M.adj[r].push_back(std::stoi(t[1 + j]));
    std::sort(M.adj[r].begin(), M.adj[r].end());
  }
  if (i < L.size() && L[i] == "END MAP") i++;

  Paths P = calculate_paths(M);
  SimState st_auth = init_sim(M);
  SimState st_obs[2] = {init_sim(M), init_sim(M)}; // viewpoint LEFT, RIGHT
  OppGoldTracker gt[2];

  auto parse_block = [&](size_t &k) -> Cmds {
    Cmds c;
    k++; // COMMAND ... START
    while (k < L.size() && L[k].rfind("COMMAND", 0) != 0) {
      auto t = split(L[k]);
      if (!t.empty()) {
        if (t[0] == "MOVE") c.moves.push_back({std::stoi(t[1].substr(1)), std::stoi(t[2])});
        else if (t[0] == "TRAIN") c.train_n = std::stoi(t[1]);
        else if (t[0] == "UPGRADE") c.upgrades.push_back(std::stoi(t[1]));
      }
      k++;
    }
    k++; // COMMAND ... END
    return c;
  };

  long long gold_abs[2] = {0, 0}, gold_max[2] = {0, 0}, gold_me_err = 0;
  long long gold_smin[2] = {0, 0}, gold_smax[2] = {0, 0}; // signed obs-auth: + = over-est
  int turns = 0, day = 0;
  while (i < L.size()) {
    if (L[i].rfind("RESULT", 0) == 0) break;
    if (L[i].rfind("TURN ", 0) != 0) { i++; continue; }
    day = std::stoi(split(L[i])[1]);
    i++;
    Cmds cl = parse_block(i);
    Cmds cr = parse_block(i);
    if (i < L.size() && L[i].rfind("TURN ", 0) == 0) i++;   // TURN d RESULT
    if (i < L.size() && L[i].rfind("TIME", 0) == 0) i++;     // TIME
    while (i < L.size() && L[i].rfind("END TURN", 0) != 0) i++; // skip result lines
    if (i < L.size()) i++;                                   // END TURN

    ResultBlock rb = step(st_auth, M, P, cl, cr);

    for (int sv = 0; sv < 2; ++sv) {
      Side me = (Side)sv, opp = opposite(me);
      const Cmds &mine = (me == Side::LEFT) ? cl : cr;
      std::vector<std::string> lines = to_send_block(rb, day);
      size_t li = 0;
      auto next = [&]() -> std::string { return li < lines.size() ? lines[li++] : std::string("END"); };
      apply_result_block(st_obs[sv], M, me, mine, gt[sv], next);

      if (!states_match(st_obs[sv], st_auth)) {
        std::cerr << "OBS MISMATCH viewpoint=" << side_char(me) << " TURN " << day
                  << " in " << path << "\n";
        dump_state("obs ", st_obs[sv]);
        dump_state("auth", st_auth);
        return 1;
      }
      gold_me_err = std::max(gold_me_err, std::llabs(st_obs[sv].gold[(int)me] - st_auth.gold[(int)me]));
      long long se = st_obs[sv].gold[(int)opp] - st_auth.gold[(int)opp]; // + = over-est
      gold_abs[sv] += std::llabs(se);
      gold_max[sv] = std::max(gold_max[sv], std::llabs(se));
      gold_smin[sv] = std::min(gold_smin[sv], se);
      gold_smax[sv] = std::max(gold_smax[sv], se);
    }
    turns++;
  }
  std::cout << "OBS OK " << path << " : " << turns
            << " turns, observable state EXACT; gold[me] max-err=" << gold_me_err
            << "; opp-gold |err| sum(L/R)=" << gold_abs[0] << "/" << gold_abs[1]
            << " max(L/R)=" << gold_max[0] << "/" << gold_max[1]
            << "; signed(obs-auth) L=[" << gold_smin[0] << "," << gold_smax[0]
            << "] R=[" << gold_smin[1] << "," << gold_smax[1] << "]\n";
  return 0;
}
#endif // OBS_TEST

int main(int argc, char **argv) {
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) files.push_back(argv[i]);
  if (files.empty()) files.push_back("Seongho/files_3/sample_log.txt");
  int rc = 0;
#ifdef OBS_TEST
  for (auto &f : files) rc |= replay_observed(f);
#else
  for (auto &f : files) rc |= replay(f);
#endif
  if (rc == 0) std::cout << "ALL OK\n";
  return rc;
}

#endif

// NEXT NATION — §4(c) architecture — StrategyManager bot.
//
// Layers (see References/StarCraft.md + plan):
//   §5 exact two-sided forward simulator  (KEYSTONE — this file, `step`)
//   §5 combat resolver / eval / opponent model
//   §4a managers -> parameterized scripted policies (candidate generators)
//   StrategyManager (§4c): candidates x K-day rollout vs mirror -> eval -> pick
//
// Build:
//   g++ -O2 -std=c++17 hybrid.cpp -o hybrid.exe          # contest bot
//   g++ -O2 -std=c++17 -DSIM_TEST hybrid.cpp -o simtest  # simulator unit test
//
// The simulator is ported 1:1 from the referee
// (Seongho/files_3/testing-tool-practice.py, run_game order). SIM_TEST replays a
// referee log and asserts the produced result block matches byte-for-byte.

#include <algorithm>
#include <cassert>
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
static int region_fight_survivors(std::vector<int> atk, std::vector<int> def,
                                  int def_turret) {
  auto tick = [](std::vector<int> &t) {
    int bi = -1;
    for (size_t i = 0; i < t.size(); ++i)
      if (t[i] > 0 && (bi < 0 || t[i] < t[bi])) bi = (int)i;
    if (bi >= 0) t[bi]--;
  };
  for (int guard = 0; guard < 1000; ++guard) {
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
                            int turret, int cap) {
  for (int m = 1; m <= cap; ++m) {
    std::vector<int> atk(m, atk_hp);
    if (region_fight_survivors(atk, def_hps, turret) > 0) return m;
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

// Per-turn world snapshot: per-region alive counts, total hp/army, and each
// side's building list. One pass, reused by eval / gen_cmds / the opponent
// model so the same aggregates aren't recomputed ad hoc.
struct WorldView {
  std::vector<int> mine, enemy;     // per-region alive warrior counts (me / opp)
  double my_hp = 0, opp_hp = 0;     // total alive warrior hp
  int my_army = 0, opp_army = 0;    // total alive warrior counts
  std::vector<const Building *> my_b, opp_b;
};
static WorldView build_world(const SimState &S, const GameMap &M, Side me) {
  WorldView w;
  w.mine.assign(M.N, 0);
  w.enemy.assign(M.N, 0);
  for (const auto &wr : S.warriors) {
    if (wr.hp <= 0) continue;
    if (wr.id.side == me) { w.mine[wr.region]++; w.my_hp += wr.hp; w.my_army++; }
    else { w.enemy[wr.region]++; w.opp_hp += wr.hp; w.opp_army++; }
  }
  for (const auto &b : S.buildings) {
    if (b.side == me) w.my_b.push_back(&b);
    else w.opp_b.push_back(&b);
  }
  return w;
}

// Score the state from `me`'s perspective. Terminal HQ loss dominates; ties at
// the day-200 horizon fall back to HQ-HP (Rule 4), which `hq_hp` diff encodes.
static double eval_state(const SimState &st, const GameMap &M, Side me, int turn) {
  Side opp = opposite(me);
  int my_hq_r = hq_region_of(M, me), op_hq_r = hq_region_of(M, opp);
  const Building *myhq = building_at(st, my_hq_r);
  const Building *ophq = building_at(st, op_hq_r);
  if (!myhq) return -1e9 + turn;  // delaying our loss is less bad
  if (!ophq) return 1e9 - turn;   // winning sooner is better

  double sc = 0;
  sc += 60.0 * (myhq->hp - ophq->hp);
  // Damage already dealt to each HQ (a half-broken enemy HQ is real progress
  // even at full-horizon draws where the hp *diff* is otherwise zero).
  sc += 15.0 * ((ophq->max_hp_now() - ophq->hp) - (myhq->max_hp_now() - myhq->hp));

  WorldView wv = build_world(st, M, me);
  const std::vector<int> &mine = wv.mine, &enemy = wv.enemy;
  double myhp = wv.my_hp, ophp = wv.opp_hp;
  sc += 4.0 * (myhp - ophp);
  sc += 1.0 * (side_income(st, me) - side_income(st, opp));

  // Closing pressure: a force on the enemy HQ that BEATS its garrison+turret is
  // a winning siege — reward it so deep self-play vs a mirror still has a
  // gradient toward finishing (instead of predicting a passive 30-30 draw).
  int my_net = mine[op_hq_r] - enemy[op_hq_r] - ophq->turret();
  int op_net = enemy[my_hq_r] - mine[my_hq_r] - myhq->turret();
  sc += 22.0 * (std::max(0, my_net) - std::max(0, op_net));

  int edge = 0;
  for (int r = 0; r < M.N; ++r) {
    if (mine[r] > enemy[r]) edge++;
    else if (enemy[r] > mine[r]) edge--;
  }
  sc += 8.0 * edge;
  sc += 0.05 * (double)(st.gold[(int)me] - st.gold[(int)opp]);
  return sc;
}

// =================== §6 managers -> parameterized policy =====================
struct Policy {
  int attack_mass;   // min warriors massed before committing a push
  int econ_reserve;  // gold kept so an upgrade doesn't starve moves/train
  int train_reserve; // gold kept back after training
  int attack_turn;   // push by this day even on a thin economy
  int late_game;     // bank HQ HP for the tiebreak after this day
  int army_extra;    // army cap = total work_cap + this
  bool deep_hq;      // upgrade HQ past L2 in midgame (economy posture)
};
namespace presets {
const Policy BALANCED{5, 200, 60, 40, 170, 6, false};
const Policy TURTLE{8, 350, 60, 90, 150, 10, true};
const Policy RUSH{3, 80, 0, 18, 175, 3, false};
const Policy EXPAND{6, 250, 60, 60, 165, 12, true};
} // namespace presets

// Per-side expansion order (strongholds reachable from that side's HQ, nearest
// first). Precomputed once; static across the game.
struct Strat {
  std::vector<int> expand_order[2];
};
static Strat build_strat(const GameMap &M, const Paths &P) {
  Strat s;
  for (int sv = 0; sv < 2; ++sv) {
    Side side = (Side)sv;
    int hq = hq_region_of(M, side);
    for (int sh : M.strongholds)
      if (reachable(P, hq, sh)) s.expand_order[sv].push_back(sh);
    std::sort(s.expand_order[sv].begin(), s.expand_order[sv].end(),
              [&](int a, int b) {
                if (P.dist[hq][a] != P.dist[hq][b]) return P.dist[hq][a] < P.dist[hq][b];
                return a < b;
              });
  }
  return s;
}

static bool is_stronghold(const GameMap &M, int region) {
  return std::binary_search(M.strongholds.begin(), M.strongholds.end(), region);
}

// Generate one day's commands for `side` under `pol`. Side-relative port of the
// §4a manager decomposition in submission/main.cpp (Economy -> Movement ->
// Military), reading the two-sided SimState. Produces referee-valid commands.
static Cmds gen_cmds(const SimState &S, const GameMap &M, const Paths &P,
                     const Strat &ST, Side side, const Policy &pol, int turn,
                     bool verbose = false) {
  Cmds a;
  int sv = (int)side;
  int my_hq = hq_region_of(M, side);
  int opp_hq = hq_region_of(M, opposite(side));
  long long budget = S.gold[sv];

  WorldView wv = build_world(S, M, side);
  std::vector<int> &mine = wv.mine, &enemy = wv.enemy;
  std::vector<const Building *> &my_b = wv.my_b;
  const Building *hq = building_at(S, my_hq);
  int my_whp = hq ? HQ_LEVELS[hq->level].warrior_hp : HQ_LEVELS[1].warrior_hp;

  // Reserve up to work_cap stationary warriors per building as laborers; the
  // rest form the mobile pool.
  std::set<int> committed;
  for (const Building *b : my_b) {
    int cap = b->work_cap(), got = 0;
    for (const auto &w : S.warriors) {
      if (got >= cap) break;
      if (w.id.side == side && w.hp > 0 && w.region == b->region &&
          w.state == WState::STATIONARY) {
        committed.insert(w.id.num);
        got++;
      }
    }
  }
  std::vector<const Warrior *> mobile;
  for (const auto &w : S.warriors)
    if (w.id.side == side && w.hp > 0 && w.state == WState::STATIONARY &&
        !committed.count(w.id.num))
      mobile.push_back(&w);

  auto drop = [&](const Warrior *w) {
    mobile.erase(std::remove(mobile.begin(), mobile.end(), w), mobile.end());
  };
  auto issue_move = [&](const Warrior *w, int dest) -> bool {
    if (w->region == dest || !reachable(P, w->region, dest)) return false;
    const Building *db = building_at(S, dest);
    int cost = (db && db->side == side) ? 0 : MOVE_COST;
    if (budget < cost) return false;
    budget -= cost;
    a.moves.push_back({w->id.num, dest});
    drop(w);
    return true;
  };
  auto nearest_mobile = [&](int region) -> const Warrior * {
    const Warrior *best = nullptr;
    long long bd = PINF;
    for (const Warrior *w : mobile) {
      if (w->region == region) continue;
      if (reachable(P, w->region, region) && P.dist[w->region][region] < bd) {
        bd = P.dist[w->region][region];
        best = w;
      }
    }
    return best;
  };
  auto waiting_to_build = [&](const Warrior *w) {
    return is_stronghold(M, w->region) && !building_at(S, w->region);
  };

  // -------- EconomyManager: build on occupied strongholds, one ROI upgrade ----
  for (int sh : ST.expand_order[sv]) {
    if (budget < BASE_BUILD_COST) break;
    if (building_at(S, sh) || enemy[sh] > 0) continue;
    const Warrior *here = nullptr;
    for (const Warrior *w : mobile)
      if (w->region == sh) { here = w; break; }
    if (here) {
      a.upgrades.push_back(sh);
      budget -= BASE_BUILD_COST;
      committed.insert(here->id.num);
      drop(here);
    }
  }
  const Building *ohq = building_at(S, opp_hq);
  int opp_hq_lvl = (ohq && ohq->side != side) ? ohq->level : 0;
  bool did_hq = false;
  if (hq && mine[my_hq] >= 1 && enemy[my_hq] == 0) {
    // Keep our HQ level at or above the opponent's: HQ level buys train_cap
    // (out-produce), work_cap (economy), turret + max-HP (defense & tiebreak).
    bool want = hq->level == 1 || turn >= pol.late_game ||
                (pol.deep_hq && hq->level < 4) ||
                (opp_hq_lvl >= hq->level && hq->level < HQ_MAX_LEVEL);
    if (hq->level < HQ_MAX_LEVEL && want) {
      int cost = HQ_LEVELS[hq->level + 1].upgrade_cost;
      if (budget >= cost + pol.econ_reserve) {
        a.upgrades.push_back(my_hq);
        budget -= cost;
        did_hq = true;
      }
    } else if (hq->level == HQ_MAX_LEVEL && turn >= pol.late_game &&
               hq->hp < hq->max_hp_now() &&
               budget >= HQ_HEAL_COST + pol.econ_reserve) {
      a.upgrades.push_back(my_hq);
      budget -= HQ_HEAL_COST;
      did_hq = true;
    }
  }
  if (!did_hq) {
    for (const Building *b : my_b) {
      if (b->type != BType::BASE || b->level >= BASE_MAX_LEVEL || enemy[b->region] > 0)
        continue;
      int cost = BASE_LEVELS[b->level + 1].cost;
      if (mine[b->region] >= b->work_cap() && budget >= cost + pol.econ_reserve) {
        a.upgrades.push_back(b->region);
        budget -= cost;
        break;
      }
    }
  }

  // -------- MovementManager: defense > fill-labor > expand > rally/attack -----
  int rally = my_hq;
  long long front = -1;
  for (const Building *b : my_b) {
    if (reachable(P, my_hq, b->region) && P.dist[my_hq][b->region] > front) {
      front = P.dist[my_hq][b->region];
      rally = b->region;
    }
  }
  // (A) ANTICIPATORY DEFENSE (focused on the HQ / 본진 — the enemy's usual target):
  // predict each enemy's target = its nearest reachable building of ours, counted
  // only if it is advancing toward us AND within an interception horizon. If a
  // building would be out-fought (incoming > defenders + turret), meet the wave
  // with defenders that ARRIVE IN TIME: surplus for any building, but LABORERS
  // only to save the HQ (pulling miners for distant outposts collapses the
  // economy). The HQ shortfall is emergency-trained below (spawns at the HQ).
  long long lane = reachable(P, my_hq, opp_hq) ? P.dist[my_hq][opp_hq] : PINF;
  long long horizon = (lane < PINF) ? lane / 2 : PINF;
  bool hq_threatened = false;
  int hq_need = 0, hq_have = 0;
  std::vector<int> incoming(M.N, 0);
  std::vector<long long> eta(M.N, PINF);
  std::vector<std::vector<int>> atk_hps(M.N);
  for (const auto &w : S.warriors) {
    if (w.hp <= 0 || w.id.side == side) continue;
    int tgt = -1;
    long long bd = PINF;
    for (const Building *b : my_b) {
      if (!reachable(P, w.region, b->region)) continue;
      long long d = P.dist[w.region][b->region];
      if (d < bd) { bd = d; tgt = b->region; }
    }
    if (tgt < 0) continue; // no reachable building of ours
    long long d_home = reachable(P, w.region, opp_hq) ? P.dist[w.region][opp_hq] : PINF;
    if (bd > d_home) continue; // not advancing toward us
    incoming[tgt]++;
    if (bd < eta[tgt]) eta[tgt] = bd;
    atk_hps[tgt].push_back(w.hp);
  }
  std::vector<const Warrior *> laborers; // draftable miners (committed, not builders)
  for (const auto &w : S.warriors)
    if (w.id.side == side && w.hp > 0 && w.state == WState::STATIONARY &&
        committed.count(w.id.num) && !waiting_to_build(&w)) {
      const Building *wb = building_at(S, w.region);
      if (wb && wb->side == side) laborers.push_back(&w);
    }
  auto recruit_to = [&](int r, std::vector<const Warrior *> &pool) -> bool {
    const Warrior *best = nullptr;
    long long bd = PINF;
    for (const Warrior *w : pool) {
      if (w->region == r || !reachable(P, w->region, r)) continue;
      long long d = P.dist[w->region][r];
      if (d <= eta[r] && d < bd) { bd = d; best = w; } // must arrive in time
    }
    if (!best || !issue_move(best, r)) return false;
    pool.erase(std::remove(pool.begin(), pool.end(), best), pool.end());
    return true;
  };
  int my_army_total = 0;
  for (int c : mine) my_army_total += c;
  std::vector<const Building *> threatened;
  for (const Building *b : my_b)
    if (incoming[b->region] > mine[b->region] + b->turret() &&
        eta[b->region] <= horizon)
      threatened.push_back(b);
  std::sort(threatened.begin(), threatened.end(),
            [&](const Building *x, const Building *y) {
              bool hx = x->region == my_hq, hy = y->region == my_hq;
              if (hx != hy) return hx;               // HQ first
              return eta[x->region] < eta[y->region]; // then most imminent
            });
  for (const Building *b : threatened) {
    int r = b->region;
    int need = min_defenders_to_hold(atk_hps[r], my_whp, b->turret(), 64);
    int have = mine[r];
    int ps = 0, pl = 0;
    while (have < need && recruit_to(r, mobile)) { have++; ps++; }   // in-time surplus
    if (r == my_hq)
      while (have < need && recruit_to(r, laborers)) { have++; pl++; } // in-time laborers
    bool total_mob = false;
    if (r == my_hq) {
      int hq_cap = HQ_LEVELS[hq->level].train_cap;
      // TOTAL MOBILIZATION: an overwhelming wave at the HQ (outnumbers our whole
      // army, beyond what one turn's emergency training can patch) -> recall
      // EVERYTHING reachable home, ignoring arrival time and the `need` cap: all
      // idle surplus + every laborer off mining. (Units already marching can't be
      // redirected by the engine, so this grabs all the rest.) HQ must not fall.
      bool overwhelmed =
          (need - have) > hq_cap && incoming[r] >= std::max(8, my_army_total);
      if (overwhelmed) {
        total_mob = true;
        auto recall = [&](std::vector<const Warrior *> &pool, int &cnt) {
          std::vector<const Warrior *> snap = pool;
          for (const Warrior *w : snap) {
            if (w->region == r || !reachable(P, w->region, r)) continue;
            if (issue_move(w, r)) {
              pool.erase(std::remove(pool.begin(), pool.end(), w), pool.end());
              have++; cnt++;
            }
          }
        };
        recall(mobile, ps);
        recall(laborers, pl);
      }
      hq_threatened = true; hq_need = need; hq_have = have;
    }
#ifdef DBG_DEF
    if (verbose)
      std::cerr << "T" << turn << " THREAT b=" << r << " incoming=" << incoming[r]
                << " eta=" << eta[r] << " need=" << need << " have=" << have
                << " pulled s/l=" << ps << "/" << pl
                << (total_mob ? " TOTAL_MOB" : "") << "\n";
#endif
  }
  for (const Building *b : my_b) { // (B) fill labor
    int r = b->region, cur = std::min(mine[r], b->work_cap());
    while (cur < b->work_cap()) {
      const Warrior *w = nearest_mobile(r);
      if (!w || !issue_move(w, r)) break;
      cur++;
    }
  }
  int inflight = 0; // (C) expand: at most one claim in flight
  for (const auto &w : S.warriors) {
    if (w.id.side != side || w.hp <= 0) continue;
    int loc = (w.state == WState::MOVING) ? w.target : w.region;
    if (is_stronghold(M, loc) && !building_at(S, loc)) inflight++;
  }
  if (inflight == 0) {
    for (int sh : ST.expand_order[sv]) {
      if (building_at(S, sh) || enemy[sh] > 0) continue;
      const Warrior *w = nearest_mobile(sh);
      if (w) issue_move(w, sh);
      break;
    }
  }
  // (D) attack: mass at front, then commit a winning wave to the enemy HQ.
  int target = opp_hq;
  const Building *eb = building_at(S, target);
  std::vector<int> def_hps;
  for (const auto &w : S.warriors)
    if (w.hp > 0 && w.region == target && w.id.side != side) def_hps.push_back(w.hp);
  int turret = (eb && eb->side != side) ? eb->turret() : 0;
  int resolver_mass = min_mass_to_take(my_whp, def_hps, turret, 60);
  int need_mass = std::max(pol.attack_mass, std::min(resolver_mass, (int)def_hps.size() + turret + 2));
  int enroute = 0;
  for (const auto &w : S.warriors)
    if (w.id.side == side && w.hp > 0 && w.state == WState::MOVING && w.target == target)
      enroute++;
  bool economy_ok = my_b.size() >= 2 || turn >= pol.attack_turn;
  int rallied = 0;
  for (const Warrior *w : mobile)
    if (w->region == rally) rallied++;
  // Once we've massed enough to attack, SUSTAIN the assault: keep streaming the
  // whole surplus army forward instead of stopping after one `need_mass` wave.
  // (The old `enroute < need_mass` throttle left a 40-strong army idle at home
  // and stalled games to a tiebreak loss.)
  bool launch = economy_ok && rallied + enroute >= need_mass;
  std::vector<const Warrior *> snap = mobile;
  for (const Warrior *w : snap) {
    if (waiting_to_build(w) || w->region == target) continue;
    if (w->region == rally) { if (launch) issue_move(w, target); }
    else issue_move(w, rally);
  }

  // -------- MilitaryManager: emergency defense first, else solvent training -----
  if (hq && HQ_LEVELS[hq->level].train_cap > 0) {
    int cap = HQ_LEVELS[hq->level].train_cap;
    // Emergency: HQ under predicted threat and still short after mobilization ->
    // spawn defenders NOW (they spawn at the HQ and join today's combat). Bypass
    // the normal army-cap / solvency gate; spend down toward the train cost.
    if (hq_threatened && hq_have < hq_need && budget >= TRAIN_COST) {
      int n = std::min({hq_need - hq_have, cap, (int)(budget / TRAIN_COST)});
      if (n > 0) {
        a.train_n = n;
        budget -= (long long)TRAIN_COST * n;
#ifdef DBG_DEF
        if (verbose)
          std::cerr << "T" << turn << " EMERGENCY_TRAIN n=" << n << "\n";
#endif
      }
    }
    int total_cap = 0, army = 0, opp_army = 0;
    for (const Building *b : my_b) total_cap += b->work_cap();
    for (const auto &w : S.warriors)
      if (w.hp > 0) (w.id.side == side ? army : opp_army)++;
    // Match the opponent's production, but PACED to when their force can actually
    // arrive: a freshly trained enemy unit is `lane` turns of marching away, and we
    // add `cap` (train_cap) units/turn, so we only need to be at parity by the time
    // they reach us. Until then keep just the economy army and let gold bank for
    // upgrades. t_arr = ETA of an advancing enemy (range-gate) else the full lane
    // (they must still march from home); needed_now spreads the catch-up over that
    // grace. ARMY_HARD_MAX bounds the O(n^2) combat cost of the lookahead.
    constexpr int ARMY_HARD_MAX = 60;
    long long t_arr = (eta[my_hq] < PINF) ? eta[my_hq] : lane;
    int parity = std::min(opp_army + 4, ARMY_HARD_MAX);       // don't fall behind
    int econ_floor = std::max(3, total_cap + pol.army_extra); // economy baseline
    int needed_now = (t_arr >= PINF)
                         ? econ_floor // opp unreachable by land -> no army threat
                         : parity - cap * (int)std::min<long long>(t_arr, parity);
    int cap_army = std::max(econ_floor, needed_now);
    int income_after = WORK_INCOME * std::min(total_cap, army + 1);
    bool sustainable = income_after - UPKEEP_PER_WARRIOR * (army + 1) >= 0;
    bool room = army < cap_army; // paced opponent-tracking cap (see above)
    long long floor = pol.train_reserve + (inflight > 0 ? BASE_BUILD_COST : 0);
    // Steady production stays solvent (the defense path emergency-trains / total-
    // mobilizes when actually under attack); cap_army keeps us matching the
    // opponent as far as our economy can sustain.
    if (a.train_n == 0 && room && sustainable && budget >= TRAIN_COST + floor) {
      int n = std::min(cap, (int)((budget - floor) / TRAIN_COST));
      n = std::min(n, cap_army - army);
      if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; }
    }
    // Conservative surplus -> HQ level: once the (paced) standing army is already
    // FULL and there is no near-term threat, spend genuine LEFTOVER gold on an HQ
    // level the EconomyManager gate didn't take. Never trades away army (the
    // production above already filled cap_army; we use only what remains), so it
    // adds upgrades only when truly flush -- e.g. army at the ARMY_HARD_MAX ceiling
    // in a rich late game.
    if (!did_hq && army >= cap_army && needed_now <= econ_floor &&
        !hq_threatened && enemy[my_hq] == 0 && hq->level < HQ_MAX_LEVEL) {
      int up_cost = HQ_LEVELS[hq->level + 1].upgrade_cost;
      if (budget >= up_cost + pol.econ_reserve) {
        a.upgrades.push_back(my_hq);
        budget -= up_cost;
        did_hq = true;
      }
    }
  }
  return a;
}

// ===================== §5 opponent model + §8 StrategyManager ================
// Opponent assumed to play the BALANCED policy (point-symmetry mirror: same
// decision rule applied from their HQ). gold[opp] is the running estimate.
//
// For each candidate policy we roll the game forward K days (us = candidate,
// them = mirror), score the terminal state, and keep the candidate with the best
// eval. We then emit that candidate's day-0 commands. Script-based lookahead
// (Justesen): search over a handful of scripts, not raw actions.
// Infer the opponent's posture from observed state so the lookahead rolls them
// forward realistically (not a fixed mirror). Full information makes this cheap:
// army pushing into our half -> RUSH; many bases -> EXPAND; small & home -> TURTLE.
//
// DISABLED (kept for the record): benchmarked vs a 6-bot pool (rush / two §4a
// managers / alt-hybrid / turtle / expand), it is WORSE than a fixed BALANCED
// mirror -- 28-6 vs 33-6, and notably 2-4 vs a turtle (6-0 for BALANCED).
// Modeling a defensive opponent makes our lookahead timid and it fails to grind
// out wins. The fixed BALANCED mirror keeps us aggressive. Do not enable.
[[maybe_unused]] static Policy infer_opp_policy(const SimState &S,
                                                const GameMap &M, const Paths &P) {
  Side opp = opposite(M.my_side);
  long long lane = reachable(P, M.my_hq, M.opp_hq) ? P.dist[M.my_hq][M.opp_hq] : PINF;
  WorldView wv = build_world(S, M, M.my_side);
  int opp_army = wv.opp_army, opp_bases = 0, opp_in_my_half = 0;
  for (const Building *b : wv.opp_b)
    if (b->type == BType::BASE) opp_bases++;
  for (const auto &w : S.warriors) {
    if (w.id.side != opp || w.hp <= 0) continue;
    if (lane < PINF && reachable(P, w.region, M.my_hq) &&
        P.dist[w.region][M.my_hq] < lane / 2)
      opp_in_my_half++;
  }
  if (opp_in_my_half >= 4) return presets::RUSH;
  if (opp_bases >= 3) return presets::EXPAND;
  if (opp_bases <= 1 && opp_army <= 6) return presets::TURTLE;
  return presets::BALANCED;
}

// StrategyManager — the top-level coordinator. Sub-managers (Economy/Movement/
// Military) live in gen_cmds(); here we pick the posture by K-day lookahead.
static Cmds strategy_manager(const SimState &S0, const GameMap &M, const Paths &P,
                             const Strat &ST, int turn) {
  static const Policy CANDS[] = {presets::BALANCED, presets::TURTLE,
                                 presets::RUSH, presets::EXPAND};
#ifdef USE_INFER
  const Policy mirror = infer_opp_policy(S0, M, P);
#else
  const Policy mirror = presets::BALANCED; // infer_opp_policy(S0,M,P): see note
#endif
  // Win-condition lookahead: roll forward toward day 200, not a 16-day snapshot.
  // Late game K = days left -> the eval IS the exact tiebreak/HQ-kill terminal;
  // mid game it's capped to fit the 100ms budget (cands x K sim-days).
  constexpr int K_MAX = 16;
  int K = std::max(1, std::min(MAX_TURN - turn, K_MAX));
  Side me = M.my_side, opp = opposite(me);

  double best = -1e18;
  Cmds best_day0;
  bool have = false;
  for (const Policy &pol : CANDS) {
    SimState s = S0;
    Cmds day0;
    for (int d = 0; d < K; ++d) {
      Cmds cme = gen_cmds(s, M, P, ST, me, pol, turn + d);
      Cmds copp = gen_cmds(s, M, P, ST, opp, mirror, turn + d);
      if (d == 0) day0 = cme;
      Cmds &cl = (me == Side::LEFT) ? cme : copp;
      Cmds &cr = (me == Side::LEFT) ? copp : cme;
      step(s, M, P, cl, cr);
      if (!building_at(s, hq_region_of(M, me)) ||
          !building_at(s, hq_region_of(M, opp)))
        break;
    }
    double e = eval_state(s, M, me, turn + K);
    if (!have || e > best) { best = e; best_day0 = day0; have = true; }
  }
  return best_day0;
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
  (void)next(); // TIME
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

int main() {
  GameMap M;
  SimState S;
  parse_init(M, S);
  Paths P = calculate_paths(M);
  Strat ST = build_strat(M, P);
  int turn;
  while (read_turn_start(turn)) {
#ifdef SCRIPT_BOT
    // Sparring opponent: run a single fixed posture with NO lookahead (reuses the
    // validated gen_cmds). Pick with -DSCRIPT_POLICY=presets::TURTLE etc.
#ifndef SCRIPT_POLICY
#define SCRIPT_POLICY presets::BALANCED
#endif
    Cmds a = gen_cmds(S, M, P, ST, M.my_side, SCRIPT_POLICY, turn);
#else
    Cmds a = strategy_manager(S, M, P, ST, turn);
#endif
#ifdef DBG_DEF
    gen_cmds(S, M, P, ST, M.my_side, presets::BALANCED, turn, true); // real-turn threat log
#endif
    emit_cmds(M, a);
    read_turn_result(S, M, a);
#ifdef DBG_ECON
    {
      int army = 0, oarmy = 0, mlvl = 1, olvl = 1;
      for (const auto &w : S.warriors)
        if (w.id.side == M.my_side) army++; else oarmy++;
      if (const Building *h = building_at(S, M.my_hq)) mlvl = h->level;
      if (const Building *h = building_at(S, M.opp_hq)) olvl = h->level;
      std::cerr << "T" << turn << " gold=" << S.gold[(int)M.my_side]
                << " army=" << army << " oarmy=" << oarmy
                << " hqlvl=" << mlvl << " ohqlvl=" << olvl << "\n";
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

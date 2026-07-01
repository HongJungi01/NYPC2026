#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

constexpr int MAX_TURN = 200;         // maximum turn (days)
constexpr int START_GOLD = 500;       // initial gold
constexpr int START_WARRIORS = 3;     // initial warriors
constexpr int MOVE_COST = 10;         // move cost
constexpr int TRAIN_COST = 120;       // train cost
constexpr int WORK_INCOME = 15;       // income per warrior
constexpr int UPKEEP_PER_WARRIOR = 2; // upkeep per warrior
constexpr int HQ_MAX_LEVEL = 5;       // HQ max level
constexpr int BASE_MAX_LEVEL = 3;     // base max level
constexpr int HQ_HEAL_COST = 1000;    // HQ fix cost
constexpr int BASE_HEAL_COST = 500;   // base fix cost

struct HqLevelEntry {
  int upgrade_cost;
  int warrior_hp;
  int hp;
  int turret;
  int train_cap;
  int work_cap;
};

struct BaseLevelEntry {
  int cost;
  int hp;
  int turret;
  int work_cap;
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

inline Side opposite(Side s) {
  return s == Side::LEFT ? Side::RIGHT : Side::LEFT;
}
inline char side_char(Side s) { return s == Side::LEFT ? 'A' : 'B'; }
inline Side parse_side_char(char c) {
  return c == 'A' ? Side::LEFT : Side::RIGHT;
}

struct WarriorId {
  Side side = Side::LEFT;
  int num = 0;
  bool operator==(const WarriorId &o) const {
    return side == o.side && num == o.num;
  }
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

  int current_hp() const {
    return type == BType::HQ ? HQ_LEVELS[level].hp : BASE_LEVELS[level].hp;
  }
  int work_cap() const {
    return type == BType::HQ ? HQ_LEVELS[level].work_cap
                             : BASE_LEVELS[level].work_cap;
  }
};

struct GameMap {
  int N = 0, K = 0;
  std::vector<long long> x, y;
  std::vector<int> strongholds;
  std::vector<std::vector<int>> adj;

  Side my_side = Side::LEFT;
  int my_hq = 0;
  int opp_hq = 0;
};

struct GameState {
  int gold = START_GOLD; // current gold
  int my_countdown = 5;  // my remaining countdowns
  int opp_countdown = 5; // opponent's remaining countdowns
  std::vector<Warrior> warriors;
  std::vector<Building> buildings;
};

struct Actions {
  int train_n = 0;
  std::vector<std::pair<WarriorId, int>> moves;
  std::vector<int> upgrades;
};

static std::string readln() {
  std::string s;
  if (!std::getline(std::cin, s))
    std::exit(0);
  return s;
}

static std::vector<std::string> tokens(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  for (std::string t; is >> t;)
    out.push_back(t);
  return out;
}

static WarriorId parse_warrior(const std::string &tok) {
  assert(!tok.empty() && (tok[0] == 'A' || tok[0] == 'B'));
  WarriorId id;
  id.side = parse_side_char(tok[0]);
  id.num = std::stoi(tok.substr(1));
  return id;
}

static std::string format_warrior(WarriorId id) {
  std::string s;
  s.push_back(side_char(id.side));
  s += std::to_string(id.num);
  return s;
}

static int hq_of(const GameMap &M, Side s) {
  return (s == Side::LEFT) ? 0 : M.N - 1;
}

static Building make_base(int region, Side s) {
  return Building{region, s, BType::BASE, 1, BASE_LEVELS[1].hp};
}

static void apply_upgrade(Building &b) {
  b.level += 1;
  b.hp = b.current_hp();
}

static int upgrade_cost(const Building &b) {
  if (b.type == BType::HQ)
    return HQ_LEVELS[b.level + 1].upgrade_cost;
  else
    return BASE_LEVELS[b.level + 1].cost;
}

static int max_level(const Building &b) {
  return b.type == BType::HQ ? HQ_MAX_LEVEL : BASE_MAX_LEVEL;
}

static void parse_init(GameMap &M, GameState &S) {
  {
    auto t = tokens(readln());
    assert(t.size() >= 2 && t[0] == "READY");
    M.my_side = (t[1] == "LEFT") ? Side::LEFT : Side::RIGHT;
  }
  {
    auto t = tokens(readln());
    M.N = std::stoi(t.at(0));
    M.K = std::stoi(t.at(1));
  }
  M.x.assign(M.N, 0);
  M.y.assign(M.N, 0);
  {
    auto t = tokens(readln()); // x_0 x_1 ... x_{N-1}
    for (int i = 0; i < M.N; ++i)
      M.x[i] = std::stoll(t.at(i));
  }
  {
    auto t = tokens(readln()); // y_0 y_1 ... y_{N-1}
    for (int i = 0; i < M.N; ++i)
      M.y[i] = std::stoll(t.at(i));
  }
  {
    auto t = tokens(readln()); // K strongholds
    M.strongholds.clear();
    M.strongholds.reserve(t.size());
    for (const auto &s : t)
      M.strongholds.push_back(std::stoi(s));
    std::sort(M.strongholds.begin(), M.strongholds.end());
  }
  M.adj.assign(M.N, {});
  for (int r = 0; r < M.N; ++r) {
    auto t = tokens(readln()); // deg n_1 n_2 ...
    int deg = std::stoi(t.at(0));
    auto &nb = M.adj[r];
    nb.reserve(deg);
    for (int j = 0; j < deg; ++j)
      nb.push_back(std::stoi(t.at(1 + j)));
    std::sort(nb.begin(), nb.end());
  }

  M.my_hq = hq_of(M, M.my_side);
  M.opp_hq = hq_of(M, opposite(M.my_side));

  S = GameState{};
  S.gold = START_GOLD;
  Side opp = opposite(M.my_side);
  for (int sfx = 1; sfx <= START_WARRIORS; ++sfx) {
    S.warriors.push_back(Warrior{.id = WarriorId{M.my_side, sfx},
                                 .region = M.my_hq,
                                 .hp = HQ_LEVELS[1].warrior_hp});
    S.warriors.push_back(Warrior{.id = WarriorId{opp, sfx},
                                 .region = M.opp_hq,
                                 .hp = HQ_LEVELS[1].warrior_hp});
  }
  S.buildings.push_back(Building{hq_of(M, Side::LEFT), Side::LEFT, BType::HQ, 1,
                                 HQ_LEVELS[1].hp});
  S.buildings.push_back(Building{hq_of(M, Side::RIGHT), Side::RIGHT, BType::HQ,
                                 1, HQ_LEVELS[1].hp});

  std::cout << "OK" << std::endl;
}

static bool read_turn_start(int &turn_index) {
  std::string line = readln();
  if (line == "FINISH")
    return false;
  auto t = tokens(line);
  assert(!t.empty() && t[0] == "START");
  turn_index = std::stoi(t.at(2));
  return true;
}

static Building *find_building(GameState &S, int region) {
  for (auto &b : S.buildings)
    if (b.region == region)
      return &b;
  return nullptr;
}

static Warrior *find_warrior(GameState &S, WarriorId id) {
  for (auto &w : S.warriors)
    if (w.id == id)
      return &w;
  return nullptr;
}

static void read_turn_result(GameState &S, const GameMap &M,
                             const Actions &submitted) {
  for (int region : submitted.upgrades) {
    Building *b = find_building(S, region);
    if (b == nullptr) {
      S.gold -= BASE_LEVELS[1].cost;
      S.buildings.push_back(make_base(region, M.my_side));
    } else {
      if (b->level >= max_level(*b)) {
        int cost = (b->type == BType::HQ) ? HQ_HEAL_COST : BASE_HEAL_COST;
        S.gold -= cost;
        b->hp = b->current_hp();
      } else {
        S.gold -= upgrade_cost(*b);
        apply_upgrade(*b);
      }
    }
  }

  for (const auto &[id, target] : submitted.moves) {
    Building *b = find_building(S, target);
    int cost = (b != nullptr && b->side == M.my_side) ? 0 : MOVE_COST;
    S.gold -= cost;
    if (Warrior *w = find_warrior(S, id)) {
      w->state = WState::MOVING;
      w->target = target;
    }
  }

  S.gold -= TRAIN_COST * submitted.train_n;

  {
    std::string line = readln();
    if (line == "FINISH")
      std::exit(0);
    auto t = tokens(line);
    assert(!t.empty() && t[0] == "TURN");
  }
  {
    auto t = tokens(readln());
    S.my_countdown = std::stoi(t.at(2));
    S.opp_countdown = std::stoi(t.at(4));
  }
  // UPGRADE
  {
    auto t = tokens(readln()); // "UPGRADE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln()); // "<A|B> <region>"
      Side s = parse_side_char(r.at(0)[0]);
      int region = std::stoi(r.at(1));
      Building *b = find_building(S, region);
      if (b == nullptr) {
        S.buildings.push_back(make_base(region, s));
      } else if (b->side != M.my_side) {
        if (b->level >= max_level(*b)) {
          b->hp = b->current_hp();
        } else {
          apply_upgrade(*b);
        }
      }
    }
  }
  // TRAIN
  {
    auto t = tokens(readln()); // "TRAIN N"
    int n = std::stoi(t.at(1));
    if (n > 0) {
      auto ids = tokens(readln());
      for (int i = 0; i < n; ++i) {
        WarriorId id = parse_warrior(ids.at(i));
        int hq_region = hq_of(M, id.side);
        Building *hq_b = find_building(S, hq_region);
        int hq_level = (hq_b != nullptr) ? hq_b->level : 1;
        S.warriors.push_back(Warrior{.id = id,
                                     .region = hq_region,
                                     .hp = HQ_LEVELS[hq_level].warrior_hp});
      }
    }
  }
  // MOVE
  {
    auto t = tokens(readln()); // "MOVE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln());
      WarriorId id = parse_warrior(r.at(0));
      int region = std::stoi(r.at(1));
      if (Warrior *w = find_warrior(S, id)) {
        w->region = region;
        if (id.side == M.my_side && w->state == WState::MOVING &&
            w->region == w->target) {
          w->state = WState::STATIONARY;
        }
      }
    }
  }
  // DAMAGE
  {
    auto t = tokens(readln()); // "DAMAGE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln());
      WarriorId id = parse_warrior(r.at(1));
      int damage = std::stoi(r.at(2));
      if (Warrior *w = find_warrior(S, id))
        w->hp -= damage;
    }
    S.warriors.erase(std::remove_if(S.warriors.begin(), S.warriors.end(),
                                    [](const Warrior &w) { return w.hp <= 0; }),
                     S.warriors.end());
  }
  // SIEGE
  {
    auto t = tokens(readln()); // "SIEGE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln());
      int region = std::stoi(r.at(1));
      int damage = std::stoi(r.at(2));
      if (Building *b = find_building(S, region))
        b->hp -= damage;
    }
    S.buildings.erase(
        std::remove_if(S.buildings.begin(), S.buildings.end(),
                       [](const Building &b) { return b.hp <= 0; }),
        S.buildings.end());
  }
  (void)readln(); // "END"

  int income = 0;
  for (const auto &b : S.buildings) {
    if (b.side != M.my_side)
      continue;
    int count = 0;
    for (const auto &w : S.warriors) {
      if (w.id.side == M.my_side && w.region == b.region)
        ++count;
    }
    income += WORK_INCOME * std::min(count, b.work_cap());
  }
  S.gold += income;

  int alive = 0;
  for (const auto &w : S.warriors)
    if (w.id.side == M.my_side)
      ++alive;
  S.gold = std::max(0, S.gold - UPKEEP_PER_WARRIOR * alive);
}

struct Paths {
  std::vector<std::vector<double>> dist;
  std::vector<std::vector<int>> nxt;
};

static double euclid_ceil(const GameMap &M, int u, int v) {
  double dx = (double)(M.x[u] - M.x[v]);
  double dy = (double)(M.y[u] - M.y[v]);
  return std::ceil(std::sqrt(dx * dx + dy * dy));
}

static Paths calculate_paths(const GameMap &M) {
  const double INF = std::numeric_limits<double>::infinity();
  Paths P;
  P.dist.assign(M.N, std::vector<double>(M.N, INF));
  P.nxt.assign(M.N, std::vector<int>(M.N, -1));

  for (int i = 0; i < M.N; ++i) {
    P.dist[i][i] = 0.0;
    P.nxt[i][i] = i;
  }
  for (int u = 0; u < M.N; ++u) {
    for (int v : M.adj[u]) {
      double w = euclid_ceil(M, u, v);
      if (w < P.dist[u][v])
        P.dist[u][v] = w;
    }
  }

  for (int k = 0; k < M.N; ++k) {
    for (int u = 0; u < M.N; ++u) {
      if (P.dist[u][k] == INF)
        continue;
      for (int v = 0; v < M.N; ++v) {
        double cand = P.dist[u][k] + P.dist[k][v];
        if (cand < P.dist[u][v])
          P.dist[u][v] = cand;
      }
    }
  }

  for (int u = 0; u < M.N; ++u) {
    for (int v = 0; v < M.N; ++v) {
      if (u == v || P.dist[u][v] == INF)
        continue;
      double best_score = INF;
      for (int nb : M.adj[u]) {
        if (P.dist[nb][v] == INF)
          continue;
        double score = euclid_ceil(M, u, nb) + P.dist[nb][v];
        if (score < best_score) {
          best_score = score;
          P.nxt[u][v] = nb;
        }
      }
    }
  }
  return P;
}

// Returns the next step on the path from u to v.
// If the path is not reachable, returns -1.
static int next_step(const Paths &P, int u, int v) { return P.nxt[u][v]; }

// Returns the path from u to v.
// If the path is not reachable, returns an empty vector.
static std::vector<int> path(const Paths &P, int u, int v) {
  std::vector<int> out;
  if (P.nxt[u][v] == -1)
    return out;
  out.push_back(u);
  while (u != v) {
    u = P.nxt[u][v];
    out.push_back(u);
  }
  return out;
}

static void emit_command() { std::cout << "COMMAND\n"; }

static void emit_actions(const Actions &a) {
  for (const auto &[id, target] : a.moves) {
    std::cout << "MOVE " << format_warrior(id) << ' ' << target << '\n';
  }
  for (int r : a.upgrades) {
    std::cout << "UPGRADE " << r << '\n';
  }
  if (a.train_n > 0) {
    std::cout << "TRAIN " << a.train_n << '\n';
  }
}

static void emit_end() { std::cout << "END" << std::endl; }

//////////////////////////////////
//// WRITE YOUR STRATEGY HERE ////
//////////////////////////////////
//
// StarCraft-style manager decomposition. A StrategyManager picks a posture
// implicitly from game phase + threat, then runs three sub-managers in priority
// order, each greedily consuming ONE shared gold budget:
//
//   EconomyManager  -> build bases / upgrade buildings (income + tiebreak HP)
//   MovementManager -> give every free warrior a role/destination
//                      (defense > fill-labor > expand > rally/attack)
//   MilitaryManager -> choose TRAIN n, kept solvent against upkeep
//
// Key game facts this encodes:
//   * income = 15 * min(warriors_here, work_cap)  -> saturate to work_cap, the
//     surplus warriors are freed for combat.
//   * combat is number-based (Lanchester): mass force, never dribble -> we rally
//     a surplus at the frontmost base before committing a wave to the enemy HQ.
//   * upkeep is 2/warrior/day -> training is gated on staying cash-flow positive.
//   * HP tiebreak (Rule 4) -> bank HQ HP in the late game.
//
// See README.md. The tunables below + the posture choice are the Phase-2
// forward-simulator seam (sweep these, simulate K days, pick the best plan).

namespace tune {
constexpr int ATTACK_MASS = 15;    // warriors rallied before committing a push
constexpr int ECON_RESERVE = 200; // gold kept so upgrades don't starve moves/train
constexpr int TRAIN_RESERVE = 60; // gold kept back after training
constexpr int ATTACK_TURN = 40;   // attack by this day even on a thin economy
constexpr int LATE_GAME = 170;    // bank HQ HP for the tiebreak after this day
} // namespace tune

static bool reachable(double d) { return d < 1e17; } // P.dist INF sentinel

static const Building *building_at(const GameState &S, int region) {
  for (const auto &b : S.buildings)
    if (b.region == region)
      return &b;
  return nullptr;
}

static bool is_stronghold(const GameMap &M, int region) {
  return std::binary_search(M.strongholds.begin(), M.strongholds.end(), region);
}

// Precomputed once in READY: strongholds reachable from my HQ, nearest first.
struct Strat {
  std::vector<int> expand_order;
};

static Strat build_strat(const GameMap &M, const Paths &P) {
  Strat s;
  for (int sh : M.strongholds)
    if (reachable(P.dist[M.my_hq][sh]))
      s.expand_order.push_back(sh);
  std::sort(s.expand_order.begin(), s.expand_order.end(), [&](int a, int b) {
    double da = P.dist[M.my_hq][a], db = P.dist[M.my_hq][b];
    if (da != db)
      return da < db;
    return a < b;
  });
  return s;
}

// Per-turn derived facts (positions are pre-move, which is what the referee uses
// to validate UPGRADE, since the build step runs before the move step).
struct WorldView {
  std::vector<int> mine;  // alive friendly warriors per region
  std::vector<int> enemy; // alive enemy warriors per region
};

static WorldView build_worldview(const GameState &S, const GameMap &M) {
  WorldView wv;
  wv.mine.assign(M.N, 0);
  wv.enemy.assign(M.N, 0);
  for (const auto &w : S.warriors) {
    if (w.hp <= 0)
      continue;
    if (w.id.side == M.my_side)
      wv.mine[w.region]++;
    else
      wv.enemy[w.region]++;
  }
  return wv;
}

static Actions decide(const GameState &S, const GameMap &M, const Paths &P,
                      const Strat &ST, int turn) {
  Actions a;
  WorldView wv = build_worldview(S, M);
  int budget = S.gold;

  std::vector<const Building *> my_buildings;
  for (const auto &b : S.buildings)
    if (b.side == M.my_side)
      my_buildings.push_back(&b);
  const Building *hq = building_at(S, M.my_hq);

  // Laborers: reserve up to work_cap stationary warriors per building (the income
  // cap). Every other stationary warrior joins the mobile pool.
  std::set<int> committed;
  for (const Building *b : my_buildings) {
    int cap = b->work_cap(), got = 0;
    for (const auto &w : S.warriors) {
      if (got >= cap)
        break;
      if (w.id.side == M.my_side && w.hp > 0 && w.region == b->region &&
          w.state == WState::STATIONARY) {
        committed.insert(w.id.num);
        got++;
      }
    }
  }
  std::vector<const Warrior *> mobile;
  for (const auto &w : S.warriors)
    if (w.id.side == M.my_side && w.hp > 0 && w.state == WState::STATIONARY &&
        !committed.count(w.id.num))
      mobile.push_back(&w);

  auto drop_mobile = [&](const Warrior *w) {
    mobile.erase(std::remove(mobile.begin(), mobile.end(), w), mobile.end());
  };
  auto issue_move = [&](const Warrior *w, int dest) -> bool {
    if (w->region == dest || !reachable(P.dist[w->region][dest]))
      return false;
    const Building *db = building_at(S, dest);
    int cost = (db && db->side == M.my_side) ? 0 : MOVE_COST;
    if (budget < cost)
      return false;
    budget -= cost;
    a.moves.emplace_back(w->id, dest);
    drop_mobile(w);
    return true;
  };
  // Nearest mobile warrior to `region`, skipping any already standing there.
  auto nearest_mobile = [&](int region) -> const Warrior * {
    const Warrior *best = nullptr;
    double bd = 1e18;
    for (const Warrior *w : mobile) {
      if (w->region == region)
        continue;
      double d = P.dist[w->region][region];
      if (reachable(d) && d < bd) {
        bd = d;
        best = w;
      }
    }
    return best;
  };
  // A warrior parked on an empty stronghold is a prospective builder: the
  // EconomyManager will build there once gold allows, so no manager may move it.
  auto waiting_to_build = [&](const Warrior *w) {
    return is_stronghold(M, w->region) && !building_at(S, w->region);
  };

  // =========================== EconomyManager ===========================
  // (1) Build a base wherever a mobile warrior already sits on an empty,
  //     enemy-free stronghold (nearest-to-HQ priority).
  for (int sh : ST.expand_order) {
    if (budget < BASE_LEVELS[1].cost)
      break;
    if (building_at(S, sh) || wv.enemy[sh] > 0)
      continue;
    const Warrior *here = nullptr;
    for (const Warrior *w : mobile)
      if (w->region == sh) {
        here = w;
        break;
      }
    if (here) {
      a.upgrades.push_back(sh);
      budget -= BASE_LEVELS[1].cost;
      committed.insert(here->id.num);
      drop_mobile(here);
    }
  }
  // (2) One ROI upgrade per turn from clear surplus. HQ L1->L2 is the strongest
  //     early buy (doubles HQ income, +turret, +HP); late game we bank HQ levels
  //     / repair for the body-count tiebreak; otherwise grow a saturated base.
  bool did_hq = false;
  if (hq && wv.mine[M.my_hq] >= 1 && wv.enemy[M.my_hq] == 0) {
    if (hq->level < HQ_MAX_LEVEL && (hq->level == 1 || turn >= tune::LATE_GAME)) {
      int cost = HQ_LEVELS[hq->level + 1].upgrade_cost;
      if (budget >= cost + tune::ECON_RESERVE) {
        a.upgrades.push_back(M.my_hq);
        budget -= cost;
        did_hq = true;
      }
    } else if (hq->level == HQ_MAX_LEVEL && turn >= tune::LATE_GAME &&
               hq->hp < hq->current_hp() &&
               budget >= HQ_HEAL_COST + tune::ECON_RESERVE) {
      a.upgrades.push_back(M.my_hq);
      budget -= HQ_HEAL_COST;
      did_hq = true;
    }
  }
  if (!did_hq) {
    for (const Building *b : my_buildings) {
      if (b->type != BType::BASE || b->level >= BASE_MAX_LEVEL ||
          wv.enemy[b->region] > 0)
        continue;
      int cost = BASE_LEVELS[b->level + 1].cost;
      if (wv.mine[b->region] >= b->work_cap() &&
          budget >= cost + tune::ECON_RESERVE) {
        a.upgrades.push_back(b->region);
        budget -= cost;
        break;
      }
    }
  }

  // =========================== MovementManager ==========================
  // Frontmost owned building = rally point (closest to the enemy by graph dist).
  int rally = M.my_hq;
  double front = -1;
  for (const Building *b : my_buildings) {
    double d = P.dist[M.my_hq][b->region];
    if (reachable(d) && d > front) {
      front = d;
      rally = b->region;
    }
  }

  // (A) DEFENSE: reinforce any building with enemies >= local defenders.
  for (const Building *b : my_buildings) {
    int r = b->region, need = wv.enemy[r], have = wv.mine[r];
    if (need <= 0)
      continue;
    while (have < need + 1) {
      const Warrior *w = nearest_mobile(r);
      if (!w || !issue_move(w, r)) // free move (own building)
        break;
      have++;
    }
  }

  // (B) FILL LABOR: top up undersaturated buildings (each laborer nets +13/day).
  for (const Building *b : my_buildings) {
    int r = b->region, cur = std::min(wv.mine[r], b->work_cap());
    while (cur < b->work_cap()) {
      const Warrior *w = nearest_mobile(r);
      if (!w || !issue_move(w, r)) // free move (own building)
        break;
      cur++;
    }
  }

  // (C) EXPAND: keep at most one base claim in flight to avoid stranding warriors.
  int inflight = 0;
  for (const auto &w : S.warriors) {
    if (w.id.side != M.my_side || w.hp <= 0)
      continue;
    int loc = (w.state == WState::MOVING) ? w.target : w.region;
    if (is_stronghold(M, loc) && !building_at(S, loc))
      inflight++;
  }
  if (inflight == 0) {
    for (int sh : ST.expand_order) {
      if (building_at(S, sh) || wv.enemy[sh] > 0)
        continue;
      const Warrior *w = nearest_mobile(sh);
      if (w)
        issue_move(w, sh); // costs MOVE_COST (stronghold isn't ours yet)
      break;
    }
  }

  // (D) ATTACK: gather backfield idlers at the front; once a mass is rallied,
  //     commit it to the enemy HQ. Builders and warriors already sieging the
  //     enemy HQ are left in place (never recalled).
  bool economy_ok = my_buildings.size() >= 2 || turn >= tune::ATTACK_TURN;
  int rallied = 0;
  for (const Warrior *w : mobile)
    if (w->region == rally)
      rallied++;
  bool launch = economy_ok && rallied >= tune::ATTACK_MASS;
  std::vector<const Warrior *> snapshot = mobile;
  for (const Warrior *w : snapshot) {
    if (waiting_to_build(w) || w->region == M.opp_hq)
      continue;
    if (w->region == rally) {
      if (launch)
        issue_move(w, M.opp_hq);
    } else {
      issue_move(w, rally); // gather (free move to own building)
    }
  }

  // =========================== MilitaryManager ==========================
  if (hq && HQ_LEVELS[hq->level].train_cap > 0) {
    int total_cap = 0, laborers = 0, army = 0;
    for (const Building *b : my_buildings) {
      total_cap += b->work_cap();
      laborers += std::min(wv.mine[b->region], b->work_cap());
    }
    (void)laborers;
    for (const auto &w : S.warriors)
      if (w.id.side == M.my_side && w.hp > 0)
        army++;
    // Solvency: a new warrior must not push daily upkeep past labor income, and
    // we cap the army relative to the economy so we don't over-train.
    int income_after = WORK_INCOME * std::min(total_cap, army + 1);
    bool sustainable = income_after - UPKEEP_PER_WARRIOR * (army + 1) >= 0;
    bool room = army < std::max(3, total_cap + 6);
    // While a base claim is in flight, hold back its cost so the economy engine
    // always gets funded before we spend gold on more warriors.
    int floor = tune::TRAIN_RESERVE + (inflight > 0 ? BASE_LEVELS[1].cost : 0);
    if (sustainable && room && budget >= TRAIN_COST + floor) {
      int n = std::min(HQ_LEVELS[hq->level].train_cap,
                       (budget - floor) / TRAIN_COST);
      if (n > 0) {
        a.train_n = n;
        budget -= TRAIN_COST * n;
      }
    }
  }

  return a;
}

int main() {
  GameMap M;
  GameState S;
  parse_init(M, S);              // initialize the game
  Paths P = calculate_paths(M); // calculate the shortest paths
  Strat ST = build_strat(M, P); // classify strongholds, expansion order

  int turn;
  while (read_turn_start(turn)) {
    Actions a = decide(S, M, P, ST, turn);
    emit_command();
    emit_actions(a);
    emit_end();
    read_turn_result(S, M, a);
  }
  return 0;
}

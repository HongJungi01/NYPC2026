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
// Planet Wars "bocsimacko" port: an EXACT forward simulator + 1-ply candidate-
// move search. Each turn we generate a handful of valid macro-moves, simulate
// each forward with the real rules (the opponent issues no new orders — a
// "units in flight" projection), evaluate the horizon state, and play the best.
// See ../References/SimilarGames.md (Planet Wars) and StarCraft.md §5.

namespace pw {

static const Building *bldg_at(const GameState &S, int r) {
  for (const auto &b : S.buildings)
    if (b.region == r)
      return &b;
  return nullptr;
}
static Building *bldg_at(GameState &S, int r) {
  for (auto &b : S.buildings)
    if (b.region == r)
      return &b;
  return nullptr;
}
static bool reach(double d) { return d < 1e17; }
static int turret_of(const Building &b) {
  return b.type == BType::HQ ? HQ_LEVELS[b.level].turret
                             : BASE_LEVELS[b.level].turret;
}

// ============================ EXACT SIMULATOR ============================
static void sim_apply_upgrade(GameState &S, Side side, int r) {
  Building *b = bldg_at(S, r);
  if (!b) {
    S.gold -= BASE_LEVELS[1].cost;
    S.buildings.push_back(Building{r, side, BType::BASE, 1, BASE_LEVELS[1].hp});
  } else if (b->side == side) {
    int ml = (b->type == BType::HQ) ? HQ_MAX_LEVEL : BASE_MAX_LEVEL;
    if (b->level >= ml) {
      S.gold -= (b->type == BType::HQ) ? HQ_HEAL_COST : BASE_HEAL_COST;
      b->hp = b->current_hp();
    } else {
      S.gold -= (b->type == BType::HQ) ? HQ_LEVELS[b->level + 1].upgrade_cost
                                       : BASE_LEVELS[b->level + 1].cost;
      b->level += 1;
      b->hp = b->current_hp();
    }
  }
}

// One focus-fire attack on `side` at `region`: lowest hp, ties by lowest num.
// Returns true if a target was hit, false if idle (no live target).
static bool damage_tick(GameState &S, int region, Side side) {
  int best = -1, best_hp = 1 << 30, best_num = 1 << 30;
  for (int i = 0; i < (int)S.warriors.size(); ++i) {
    const Warrior &w = S.warriors[i];
    if (w.region != region || w.id.side != side || w.hp <= 0)
      continue;
    if (w.hp < best_hp || (w.hp == best_hp && w.id.num < best_num)) {
      best_hp = w.hp;
      best_num = w.id.num;
      best = i;
    }
  }
  if (best < 0)
    return false;
  S.warriors[best].hp -= 1;
  return true;
}

// Combat + siege (referee order). Buildings at 0hp are removed here.
static void sim_combat(GameState &S, const GameMap &M) {
  std::vector<int> lc(M.N, 0), rc(M.N, 0);
  std::vector<char> touch(M.N, 0);
  for (const auto &w : S.warriors)
    if (w.hp > 0) {
      (w.id.side == Side::LEFT ? lc : rc)[w.region]++;
      touch[w.region] = 1;
    }
  for (const auto &b : S.buildings)
    touch[b.region] = 1;

  std::vector<int> siege(M.N, 0);
  for (int r = 0; r < M.N; ++r) {
    if (!touch[r])
      continue;
    Building *b = bldg_at(S, r);
    bool bL = b && b->side == Side::LEFT, bR = b && b->side == Side::RIGHT;
    int turret = b ? turret_of(*b) : 0;
    bool lp = lc[r] > 0 || bL, rp = rc[r] > 0 || bR;
    if (!(lp && rp))
      continue;
    int lcap = lc[r] + (bL ? turret : 0), rcap = rc[r] + (bR ? turret : 0);
    int lidle = 0, ridle = 0;
    for (int i = 0; i < lcap; ++i)
      if (!damage_tick(S, r, Side::RIGHT))
        lidle++;
    for (int i = 0; i < rcap; ++i)
      if (!damage_tick(S, r, Side::LEFT))
        ridle++;
    if (b) {
      int aidle = (b->side == Side::LEFT) ? ridle : lidle;
      if (aidle > 0)
        siege[r] = aidle;
    }
  }
  for (int r = 0; r < M.N; ++r)
    if (siege[r] > 0)
      if (Building *b = bldg_at(S, r))
        b->hp -= std::min(siege[r], b->hp);
  S.buildings.erase(std::remove_if(S.buildings.begin(), S.buildings.end(),
                                   [](const Building &b) { return b.hp <= 0; }),
                    S.buildings.end());
}

// Resolve one full day. myCmds applied for M.my_side; opponent issues nothing
// (its existing units still move?/fight/etc. — but we don't know its move
// intents, so opponent units that are MOVING in our belief simply continue).
static void sim_day(GameState &S, const GameMap &M, const Paths &P,
                    const Actions &myCmds, int &my_hunger) {
  Side me = M.my_side;
  // 1. upgrades (mine)
  for (int r : myCmds.upgrades)
    sim_apply_upgrade(S, me, r);
  // 2. moves (mine): pay + mark moving
  for (const auto &mv : myCmds.moves) {
    const Building *db = bldg_at(S, mv.second);
    S.gold -= (db && db->side == me) ? 0 : MOVE_COST;
    for (auto &w : S.warriors)
      if (w.id == mv.first) {
        w.state = WState::MOVING;
        w.target = mv.second;
      }
  }
  // 3. train charge
  S.gold -= TRAIN_COST * myCmds.train_n;
  // 4. day movement (all warriors), blocked if an enemy shares the start cell
  std::vector<char> hasL(M.N, 0), hasR(M.N, 0);
  for (const auto &w : S.warriors)
    if (w.hp > 0)
      (w.id.side == Side::LEFT ? hasL : hasR)[w.region] = 1;
  for (auto &w : S.warriors) {
    if (w.hp <= 0 || w.state != WState::MOVING)
      continue;
    if (w.region == w.target) {
      w.state = WState::STATIONARY;
      continue;
    }
    bool blocked = (w.id.side == Side::LEFT) ? hasR[w.region] : hasL[w.region];
    if (blocked)
      continue;
    int nx = P.nxt[w.region][w.target];
    if (nx < 0)
      continue;
    w.region = nx;
    if (w.region == w.target)
      w.state = WState::STATIONARY;
  }
  // 5. spawn trained (mine) at HQ with current HQ-level warrior hp
  if (myCmds.train_n > 0) {
    int hqr = hq_of(M, me);
    const Building *h = bldg_at(S, hqr);
    int lvl = h ? h->level : 1;
    int nextn = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == me)
        nextn = std::max(nextn, w.id.num);
    for (int i = 0; i < myCmds.train_n; ++i)
      S.warriors.push_back(Warrior{WarriorId{me, ++nextn}, hqr,
                                   HQ_LEVELS[lvl].warrior_hp,
                                   WState::STATIONARY, 0});
  }
  // 6+7. combat + siege
  sim_combat(S, M);
  // 8. work income (mine)
  for (const auto &b : S.buildings)
    if (b.side == me) {
      int cnt = 0;
      for (const auto &w : S.warriors)
        if (w.hp > 0 && w.id.side == me && w.region == b.region)
          cnt++;
      S.gold += WORK_INCOME * std::min(cnt, b.work_cap());
    }
  // 9. upkeep (mine), in warrior-number order (opponent assumed solvent)
  std::vector<int> idx;
  for (int i = 0; i < (int)S.warriors.size(); ++i)
    if (S.warriors[i].id.side == me && S.warriors[i].hp > 0)
      idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    return S.warriors[a].id.num < S.warriors[b].id.num;
  });
  for (int i : idx) {
    if (S.gold >= UPKEEP_PER_WARRIOR)
      S.gold -= UPKEEP_PER_WARRIOR;
    else {
      S.warriors[i].hp -= 1;
      my_hunger++;
    }
  }
  // remove dead warriors
  S.warriors.erase(std::remove_if(S.warriors.begin(), S.warriors.end(),
                                  [](const Warrior &w) { return w.hp <= 0; }),
                   S.warriors.end());
}

// ============================ EVALUATION =================================
static double evaluate(const GameState &S, const GameMap &M, const Paths &P) {
  int myhq = hq_of(M, M.my_side), ohq = M.opp_hq;
  const Building *mh = bldg_at(S, myhq), *oh = bldg_at(S, ohq);
  if (!mh)
    return -1e9; // my HQ destroyed
  if (!oh)
    return 1e9; // enemy HQ destroyed
  double v = (mh->hp - oh->hp) * 1000.0;
  int wc = 0;
  for (const auto &b : S.buildings)
    if (b.side == M.my_side)
      wc += b.work_cap();
  v += 30.0 * wc + 1.0 * S.gold;
  double maxd = 1.0;
  for (int r = 0; r < M.N; ++r)
    if (reach(P.dist[myhq][r]))
      maxd = std::max(maxd, P.dist[myhq][r]);
  double pos = 0;
  for (const auto &w : S.warriors) {
    if (w.id.side == M.my_side) {
      v += 20.0 * w.hp;
      double d = P.dist[w.region][ohq];
      if (reach(d))
        pos += (maxd - d);
    } else {
      v -= 20.0 * w.hp;
    }
  }
  v += 0.5 * pos / maxd;
  for (const auto &b : S.buildings)
    v += (b.side == M.my_side) ? 120.0 : -120.0;
  return v;
}

static double eval_plan(const GameState &S0, const GameMap &M, const Paths &P,
                        const Actions &day1, int horizon) {
  GameState st = S0;
  int hunger = 0;
  sim_day(st, M, P, day1, hunger);
  Actions empty;
  for (int d = 1; d < horizon; ++d) {
    if (!bldg_at(st, hq_of(M, M.my_side)) || !bldg_at(st, M.opp_hq))
      break;
    sim_day(st, M, P, empty, hunger);
  }
  return evaluate(st, M, P) - 5000.0 * hunger;
}

// ===================== CANDIDATE MOVE GENERATION =========================
// Produce ONE valid command set for the given strategic knobs.
//   trainMode  0:none 1:train up to cap/affordable
//   expandMode 0:none 1:claim nearest holdable unowned stronghold
//   armyMode   0:hold 1:reinforce front 2:mass surplus to enemy HQ
static Actions make_candidate(const GameState &S, const GameMap &M,
                              const Paths &P, int trainMode, int expandMode,
                              int armyMode) {
  Actions a;
  int budget = S.gold;
  std::vector<int> myCnt(M.N, 0), enCnt(M.N, 0);
  for (const auto &w : S.warriors)
    if (w.hp > 0)
      (w.id.side == M.my_side ? myCnt : enCnt)[w.region]++;
  std::vector<const Warrior *> mob;
  for (const auto &w : S.warriors)
    if (w.id.side == M.my_side && w.hp > 0 && w.state == WState::STATIONARY)
      mob.push_back(&w);
  std::set<int> usedNum, usedUp;
  const Building *hq = bldg_at(S, M.my_hq);
  std::vector<const Building *> myB;
  for (const auto &b : S.buildings)
    if (b.side == M.my_side)
      myB.push_back(&b);

  auto friendlyBldg = [&](int r) {
    const Building *b = bldg_at(S, r);
    return b && b->side == M.my_side;
  };
  auto issue = [&](const Warrior *w, int dest) -> bool {
    if (!w || usedNum.count(w->id.num))
      return false;
    if (w->region == dest) { // already there: commit, no command
      usedNum.insert(w->id.num);
      return true;
    }
    if (!reach(P.dist[w->region][dest]))
      return false;
    int c = friendlyBldg(dest) ? 0 : MOVE_COST;
    if (budget < c)
      return false;
    budget -= c;
    a.moves.emplace_back(w->id, dest);
    usedNum.insert(w->id.num);
    return true;
  };
  auto nearestMob = [&](int r) -> const Warrior * {
    const Warrior *best = nullptr;
    double bd = 1e18;
    for (const Warrior *w : mob) {
      if (usedNum.count(w->id.num) || w->region == r)
        continue;
      double d = P.dist[w->region][r];
      if (reach(d) && d < bd) {
        bd = d;
        best = w;
      }
    }
    return best;
  };

  // (A) DEFENSE: reinforce buildings where enemies >= local defenders.
  for (const Building *b : myB) {
    int r = b->region;
    if (enCnt[r] <= 0)
      continue;
    int have = myCnt[r];
    while (have < enCnt[r] + 1) {
      const Warrior *w = nearestMob(r);
      if (!w || !issue(w, r))
        break;
      have++;
    }
  }
  // (B) BUILD on any stronghold a mobile warrior already stands on (enemy-free).
  for (int r : M.strongholds) {
    if (budget < BASE_LEVELS[1].cost)
      break;
    if (bldg_at(S, r) || enCnt[r] > 0)
      continue;
    const Warrior *here = nullptr;
    for (const Warrior *w : mob)
      if (!usedNum.count(w->id.num) && w->region == r) {
        here = w;
        break;
      }
    if (here) {
      a.upgrades.push_back(r);
      usedUp.insert(r);
      budget -= BASE_LEVELS[1].cost;
      usedNum.insert(here->id.num);
    }
  }
  // one ROI upgrade from surplus: HQ L1->L2 (doubles HQ income) when affordable.
  if (hq && myCnt[M.my_hq] >= 1 && enCnt[M.my_hq] == 0 && !usedUp.count(M.my_hq) &&
      hq->level < HQ_MAX_LEVEL &&
      budget >= HQ_LEVELS[hq->level + 1].upgrade_cost + 150 && hq->level == 1) {
    a.upgrades.push_back(M.my_hq);
    usedUp.insert(M.my_hq);
    budget -= HQ_LEVELS[hq->level + 1].upgrade_cost;
  }
  // (C) EXPAND: send one warrior to the nearest holdable unowned stronghold.
  if (expandMode) {
    int best = -1;
    double bd = 1e18;
    for (int r : M.strongholds) {
      if (bldg_at(S, r) || enCnt[r] > 0)
        continue;
      if (P.dist[M.my_hq][r] > P.dist[M.opp_hq][r])
        continue; // enemy reaches first -> not holdable
      double d = P.dist[M.my_hq][r];
      if (reach(d) && d < bd) {
        bd = d;
        best = r;
      }
    }
    if (best >= 0)
      issue(nearestMob(best), best);
  }
  // (D) FILL LABOR: top up undersaturated buildings (each laborer nets +13/day).
  for (const Building *b : myB) {
    int r = b->region, cur = std::min(myCnt[r], b->work_cap());
    while (cur < b->work_cap()) {
      const Warrior *w = nearestMob(r);
      if (!w || !issue(w, r))
        break;
      cur++;
    }
  }
  // (E) ARMY: dispose of the remaining surplus.
  if (armyMode == 1) { // reinforce the frontmost owned building
    int rally = M.my_hq;
    double fr = -1;
    for (const Building *b : myB) {
      double d = P.dist[M.my_hq][b->region];
      if (reach(d) && d > fr) {
        fr = d;
        rally = b->region;
      }
    }
    for (const Warrior *w : mob)
      if (!usedNum.count(w->id.num))
        issue(w, rally);
  } else if (armyMode == 2) { // mass to the enemy HQ
    for (const Warrior *w : mob)
      if (!usedNum.count(w->id.num))
        issue(w, M.opp_hq);
  }
  // (F) TRAIN
  if (trainMode && hq) {
    int n = std::min(HQ_LEVELS[hq->level].train_cap, budget / TRAIN_COST);
    if (n > 0) {
      a.train_n = n;
      budget -= TRAIN_COST * n;
    }
  }
  return a;
}

} // namespace pw

static Actions decide(const GameState &S, const GameMap &M, const Paths &P,
                      int turn) {
  (void)turn;
  const int HORIZON = 16;
  Actions best;
  double bestv = -1e18;
  bool have = false;
  for (int tm = 0; tm <= 1; ++tm)
    for (int em = 0; em <= 1; ++em)
      for (int am = 0; am <= 2; ++am) {
        Actions cand = pw::make_candidate(S, M, P, tm, em, am);
        double v = pw::eval_plan(S, M, P, cand, HORIZON);
        if (!have || v > bestv) {
          bestv = v;
          best = cand;
          have = true;
        }
      }
  return best;
}

int main() {
  GameMap M;
  GameState S;
  parse_init(M, S);              // initialize the game
  Paths P = calculate_paths(M); // calculate the shortest paths

  int turn;
  while (read_turn_start(turn)) {
    Actions a = decide(S, M, P, turn);
    emit_command();
    emit_actions(a);
    emit_end();
    read_turn_result(S, M, a);
  }
  return 0;
}

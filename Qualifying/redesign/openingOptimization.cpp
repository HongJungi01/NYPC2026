// NEXT NATION — 기능 #1 (최소형): openingOptimization
//   = 공식 스켈레톤(SampleBot/sample-code.cpp 그대로) + farthest-in-window + JIT 훈련·출발.
//
// 사용자 지시(07-06)로 두 메커니즘 외 전부 삭제: 시뮬레이터/월드모델 하니스(SIM/OBS),
// 캐스케이드 릴레이, 중앙 고정(force_center), 고정 오프닝 특례, 상대 레이스 예측 없음.
// 남긴 것 = redesign.md §1의 두 시계 정렬 코어:
//   T_build = max(gold_eta, hops).
//   - farthest-in-window: 창 = 풀 골드사이클(≈300/net, 정착민의 공짜 보행 예산). 창 안
//     (hops <= horizon)이면 가장 먼(최전방) 요새(거리 공짜), 밖이면 최근접(골드 유휴 최소).
//     창 판정은 반드시 홉수(턴). (07-06: 잔여골드 톱니 gold_eta → 풀사이클 horizon 교체.)
//   - JIT 훈련·출발: 보낼 잉여 몸이 없을 때만 훈련하되, 도착(hops) >= gold_eta_train
//     (= ceil((300+120+10-gold)/(net-2)); 07-06 통찰 반영 — 신병 upkeep −2/턴과 파견
//     이동비 10을 현금흐름에 포함) 일 때만 뽑는다 — 미리 뽑으면 걷고 기다리는 동안
//     upkeep만 순증발(§1(2)). 잉여 몸의 조기 출발은 §1(1) 명시대로 무료(도착 대기 허용).
//     골드 시계도 같은 보정: 당일 건설분 +15 선반영, 파견비 10 선반영.
// 고정 오프닝(설계 §1, 07-06): 두 번째 기지에 한해 중앙점(등거리 기하중심)이 잡을 수
// 있으면 중앙 우선, 아니면 그대로. (첫 기지=최근접, 그 외=farthest-in-window.)
// 표적 = 건물이 지어져 있지 않은 모든 거점(07-06 사용자 수정: 우리쪽 절반 제한 제거).
// 빈 거점 소진 시 매턴 빈 COMMAND(패스) = 이 기능의 종결 상태.
// 확장 = 전방투영 파이프라인(07-06): 직렬 게이트 제거, 여러 정착민 동시 in-flight.
//   커밋된 정착민(되돌릴 수 없음)을 계획에 고정 → 경제를 미래로 투영해 다음 건설의
//   골드 슬롯을 산정 → 잉여는 미리 배치(무료 사전배치로 보행 지연 제거), 신병은
//   도착=골드슬롯이 되도록 JIT 훈련. 매 턴 현실 반영해 재투영(receding horizon).
//
// Build: g++ -O2 -std=c++17 -static redesign/openingOptimization.cpp -o redesign/openingOptimization.exe
// Test : redesign\run10.ps1 -Exe .\openingOptimization.exe  (랜덤시드 10판 vs simulator/sample-code.py)

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
// 턴 간 지속 상태. jit_target = 지난 턴 §6가 JIT 훈련한 유닛의 커밋 표적(이번 턴
// 파견 시 재선택 않고 그리로 → 훈련 타이밍과 실제 도착 일치, 도착지 대기 방지).
struct ExpState {
  int jit_target = -1;
};

// 기능 #1: farthest-in-window + JIT 훈련·출발 (redesign.md §1 두 시계 코어만).
static Actions decide(const GameState &S, const GameMap &M, const Paths &P,
                      int turn, ExpState &es) {
  Actions a;
  (void)turn;
  const Side me = M.my_side;
  long long gold = S.gold;

  auto building_at = [&](int r) -> const Building * {
    for (const auto &b : S.buildings)
      if (b.region == r)
        return &b;
    return nullptr;
  };
  auto is_stronghold = [&](int r) {
    return std::binary_search(M.strongholds.begin(), M.strongholds.end(), r);
  };
  auto enemy_warrior_at = [&](int r) {
    for (const auto &w : S.warriors)
      if (w.id.side != me && w.region == r)
        return true;
    return false;
  };
  // 레퍼리 실경로(가중 최단, nxt) 위의 홉수 = 이동 소요 턴. 창·JIT 판정은 반드시
  // 이 턴 단위로(가중거리 P.dist를 턴으로 쓰면 창이 안 열림 — §1 ★).
  auto hops = [&](int u, int v) -> long long {
    if (u == v)
      return 0;
    if (P.nxt[u][v] == -1)
      return -1;
    long long n = 0;
    int cur = u;
    while (cur != v) {
      cur = P.nxt[cur][v];
      ++n;
    }
    return n;
  };

  // 노동자 파티션: 각 내 건물의 work_cap을 '최저HP 정지 유닛'부터 채운 게 노동자,
  // 나머지 정지 유닛 = 잉여(정착민 후보). 지역 유닛수>cap일 때 전원을 잉여로 보던
  // 버그 방지 — 반드시 딱 cap기만 노동자로 남기고 나머지만 뺀다.
  std::set<int> worker;
  for (const auto &b : S.buildings) {
    if (b.side != me)
      continue;
    std::vector<const Warrior *> on;
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == b.region &&
          w.state == WState::STATIONARY)
        on.push_back(&w);
    std::sort(on.begin(), on.end(),
              [](const Warrior *x, const Warrior *y) { return x->hp < y->hp; });
    for (int i = 0; i < b.work_cap() && i < (int)on.size(); ++i)
      worker.insert(on[i]->id.num);
  }

  // ── 1) 건설: 빈 요새 위에 서 있는 내 유닛 + 골드 300 → UPGRADE (체인 완결) ──
  std::vector<int> built;    // 이번 턴 UPGRADE 발령한 요새 (중복 발령 = WA 방지)
  std::vector<int> used;     // 이번 턴 명령을 소진한 내 유닛 suffix
  for (const auto &w : S.warriors) {
    if (w.id.side != me || w.state != WState::STATIONARY)
      continue;
    int r = w.region;
    if (!is_stronghold(r) || building_at(r) || enemy_warrior_at(r))
      continue;
    if (std::find(built.begin(), built.end(), r) != built.end())
      continue;
    if (gold < BASE_LEVELS[1].cost)
      break;
    a.upgrades.push_back(r);
    built.push_back(r);
    used.push_back(w.id.num);
    gold -= BASE_LEVELS[1].cost;
  }

  // ── 2) 경제 스냅샷 ────────────────────────────────────────────────────────
  // 현재 수입 + 이번 턴 건설분(built)의 당일 저녁 +15(건설병 즉시 노동) 선반영.
  long long income = 0;
  for (const auto &b : S.buildings) {
    if (b.side != me)
      continue;
    int count = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.region == b.region)
        ++count;
    income += (long long)WORK_INCOME * std::min(count, b.work_cap());
  }
  income += (long long)WORK_INCOME * (long long)built.size();
  int alive = 0;
  for (const auto &w : S.warriors)
    if (w.id.side == me)
      ++alive;

  // ── 3) 커밋된 파이프라인: 이미 향하는/대기 중인 정착민 = 되돌릴 수 없는 계획 ──
  // (07-06 파이프라인 재설계) 직렬 게이트 제거. 이동 중(MOVING)이거나 빈 거점에서
  // 골드 대기 중(STATIONARY)인 내 정착민은 각자 그 거점을 '건설 예정'으로 커밋한
  // 상태(Rule 169: 이동 중 재명령 불가). committed = 그 거점 집합, 각 건설의 잔여
  // 도착 턴 parr[]도 모은다(전방투영에서 골드 슬롯과 도착을 맞추기 위함).
  std::vector<int> committed;    // 커밋된 빈 거점 (중복 파견 방지)
  std::vector<long long> parr;   // 각 커밋 건설의 잔여 도착 턴수(대기 중 = 0)
  for (const auto &w : S.warriors) {
    if (w.id.side != me)
      continue;
    if (std::find(used.begin(), used.end(), w.id.num) != used.end())
      continue; // 이번 턴 건설 완료분
    int loc = (w.state == WState::MOVING) ? w.target : w.region;
    if (!is_stronghold(loc) || building_at(loc) || enemy_warrior_at(loc))
      continue;
    if (std::find(built.begin(), built.end(), loc) != built.end())
      continue;
    committed.push_back(loc);
    long long rem = (w.state == WState::MOVING) ? hops(w.region, loc) : 0;
    parr.push_back(std::max(0LL, rem));
    if (w.state == WState::STATIONARY)
      used.push_back(w.id.num); // 도착·대기 정착민 고정(재파견 금지 = 이동잠금 준수)
  }
  std::sort(parr.begin(), parr.end());

  // ── 4) 후보 R = 빈 거점(건물X, 커밋X) + hops. 창은 슬롯별로 동적 산정한다. ─────
  struct Cand {
    int r;
    long long h;
  };
  std::vector<Cand> R;
  for (int sh : M.strongholds) {
    if (building_at(sh) || enemy_warrior_at(sh))
      continue;
    if (std::find(built.begin(), built.end(), sh) != built.end())
      continue;
    if (std::find(committed.begin(), committed.end(), sh) != committed.end())
      continue;
    long long h = hops(M.my_hq, sh);
    if (h < 0)
      continue;
    R.push_back({sh, h});
  }

  // 전방투영 horizon = 새 정착민(P+1번째 건설)의 300이 '지금부터' 준비되는 턴 수 =
  // 그 정착민의 '무손해 공짜 보행 예산'. 진행 중인 건설(parr)을 각자 도착 후 골드
  // 나는 대로 완공(+15 복리)한 뒤 남는 골드로 새 건설이 서는 턴. payTrain이면 신병
  // 훈련120+파견10 선지출·upkeep+1.
  //  - 오프닝(대기 없음·골드 넉넉): 0 근처 → 창 좁음 → 최근접(준비된 시작골드 즉시
  //    활용, 첫 기지 손해 0 = 설계 §1 '첫 건물 최근접').
  //  - 골드-bound(대기 건설 뒤에 줄섬): 한 사이클+ → 창 넓음 → 최전방(거리 공짜).
  //  - 후반(net 큼): 사이클 짧음 → 창 좁음 → 안쪽 정리. parr는 파견마다 갱신되므로
  //    같은 턴 첫 몸은 근접, 뒤 몸은 깊은 슬롯 → 자연히 더 멀리.
  auto gold_ready = [&](bool payTrain) -> long long {
    std::vector<long long> pa = parr;
    std::sort(pa.begin(), pa.end());
    long long g = gold - (payTrain ? (long long)(TRAIN_COST + MOVE_COST) : 0);
    long long inc = income;
    long long uc = alive + (payTrain ? 1 : 0);
    size_t done = 0;
    for (long long t = 0; t <= MAX_TURN; ++t) {
      if (t > 0)
        g += inc - (long long)UPKEEP_PER_WARRIOR * uc;
      while (done < pa.size() && pa[done] <= t &&
             g >= (long long)BASE_LEVELS[1].cost) {
        g -= (long long)BASE_LEVELS[1].cost;
        inc += WORK_INCOME;
        ++done;
      }
      if (done >= pa.size() && g >= (long long)BASE_LEVELS[1].cost)
        return t;
    }
    return MAX_TURN + 1;
  };
  // 남은 후보 중 farthest-in-window(창 안=최전방, 창 밖=최근접) 하나 선택.
  auto pick = [&](long long horizon, const std::set<int> &taken) -> int {
    int best = -1;
    long long bh = 0;
    bool bin = false;
    for (const Cand &c : R) {
      if (taken.count(c.r))
        continue;
      bool in = c.h <= horizon;
      bool better;
      if (best < 0)
        better = true;
      else if (in != bin)
        better = in; // 창 안 우선
      else if (in)
        better = c.h > bh || (c.h == bh && c.r < best); // 창 안: farthest
      else
        better = c.h < bh || (c.h == bh && c.r < best); // 창 밖: nearest
      if (better) {
        best = c.r;
        bh = c.h;
        bin = in;
      }
    }
    return best;
  };

  // 고정 오프닝(설계 §1): 두 번째 기지에 한해 중앙점 우선. 중앙점 = 등거리 기하중심 =
  // 우리쪽/중앙(doo>=dm) 요새 중 |doo-dm| 최소(가장 등거리), 동률이면 더 가까운 쪽.
  // (가중거리 P.dist로 판정 — 중앙 판별은 기하 고정속성이라 홉수 아님.)
  int true_center = -1;
  {
    const double INF = std::numeric_limits<double>::infinity();
    double bc = INF, bt = INF;
    for (int sh : M.strongholds) {
      if (P.dist[M.my_hq][sh] == INF)
        continue;
      double dm = P.dist[M.my_hq][sh];
      double doo = P.dist[M.opp_hq][sh]; // INF면 우리쪽 취급
      if (doo < dm)
        continue; // 우리쪽/중앙만
      double key = (doo == INF) ? dm : (doo - dm);
      if (key < bc || (key == bc && dm < bt)) {
        bc = key;
        bt = dm;
        true_center = sh;
      }
    }
  }
  int nbase = 0;
  for (const auto &b : S.buildings)
    if (b.side == me && b.type == BType::BASE)
      ++nbase;
  std::set<int> taken;
  int newly = 0;
  auto center_in_R = [&]() -> bool {
    if (true_center < 0 || taken.count(true_center))
      return false;
    for (const Cand &c : R)
      if (c.r == true_center)
        return true; // R에 있음 = 빈 거점·도달가능·미커밋
    return false;
  };
  // 다음 지을 기지의 순번(기존+진행중+이번턴 파견 다음). rank==2(두 번째 기지)이고
  // 중앙점이 잡을 수 있으면 중앙 우선, 아니면 farthest-in-window 그대로(사용자 조건).
  auto choose_target = [&](long long horizon) -> int {
    int rank = nbase + (int)built.size() + (int)committed.size() + newly + 1;
    if (rank == 2 && center_in_R())
      return true_center;
    return pick(horizon, taken);
  };

  // 표적으로 최근접 잉여 몸을 파견(성공 시 parr·taken·gold 갱신). 몸 없거나 골드
  // 부족이면 false.
  auto dispatch_to = [&](int tgt) -> bool {
    const Warrior *body = nullptr;
    long long best = -1;
    for (const auto &w : S.warriors) {
      if (w.id.side != me || w.state != WState::STATIONARY)
        continue;
      if (worker.count(w.id.num))
        continue; // 노동자(수입 기여) = 잉여 아님
      if (std::find(used.begin(), used.end(), w.id.num) != used.end())
        continue;
      if (enemy_warrior_at(w.region))
        continue; // 적 동거 = 핀
      long long h = hops(w.region, tgt);
      if (h < 0)
        continue;
      if (body == nullptr || h < best) {
        body = &w;
        best = h;
      }
    }
    if (body == nullptr || gold < MOVE_COST)
      return false;
    a.moves.emplace_back(body->id, tgt);
    used.push_back(body->id.num);
    parr.push_back(best); // 도착 = 이 정착민 건설 예정 (다음 슬롯 horizon 확장)
    taken.insert(tgt);
    gold -= MOVE_COST;
    ++newly;
    return true;
  };

  // ── 5) 잉여 파이프라인 파견 ─────────────────────────────────────────────────
  // 먼저: 지난 턴 §6가 JIT 훈련한 유닛을 그 커밋 표적으로 파견(재선택 금지). §6가 도착
  // =골드슬롯이 되도록 계산해 뽑았으니, §5가 더 가까운 표적으로 돌리면 조기 도착·대기가
  // 생긴다(사용자 지적) → 커밋 표적을 그대로 이행.
  if (es.jit_target >= 0) {
    int jt = es.jit_target;
    es.jit_target = -1;
    if (!taken.count(jt)) {
      bool inR = false;
      for (const Cand &c : R)
        if (c.r == jt) {
          inR = true;
          break;
        }
      if (inR)
        dispatch_to(jt); // 표적 유효할 때만(이미 건설·커밋됐으면 스킵)
    }
  }
  // 나머지: 슬롯마다 horizon 재산정 → 첫 몸은 근접(오프닝), 뒤 몸은 깊은 슬롯이라 더
  // 멀리. 잉여 사전배치는 무료(upkeep은 어차피 나감)+도착 대기 = 골드 슬롯 즉시 건설.
  for (;;) {
    if (taken.size() >= R.size())
      break;
    long long horizon = gold_ready(false);
    int tgt = choose_target(horizon);
    if (tgt < 0)
      break;
    if (!dispatch_to(tgt))
      break; // 잉여 소진 → 나머지는 훈련(§6)
  }

  // ── 6) JIT 파이프라인 훈련: 잉여로 못 채운 다음 표적(훈련 horizon 기준 farthest-
  // in-window)을 도착이 그 건설의 골드 슬롯 이후가 될 때만 뽑는다. 도착(hops+1) >=
  // horizon이면 지금 뽑아야 유휴 없음(골드 이미/곧 남), 반대면 대기(신병 upkeep 누수 방지).
  if (taken.size() < R.size() && a.train_n == 0 && gold >= TRAIN_COST) {
    long long horizon = gold_ready(true);
    // 훈련 유닛은 '도착 다음 턴'에 건설(buildable = 도착+1, 밥값 지불 후 명령) → 창을
    // buildable 기준 h+1<=horizon = h<=horizon-1 로. (§5 잉여는 당턴 파견이라 h<=horizon
    // 그대로.) 이 -1이 없으면 1홉 과대 표적을 골라 조기 훈련 → 도착지 대기(밥값 누수).
    int tgt = choose_target(horizon - 1); // 두 번째 기지면 중앙 우선(§1)
    if (tgt >= 0) {
      long long deliver = hops(M.my_hq, tgt) + 1;
      // 대기 건설 우선권: 훈련비 120이 '가장 먼저 짓는' 대기 정착민의 완공을 늦추면
      // 뽑지 않는다 (사용자: A4가 도착해 딱 지어야 하는데 전날 JIT가 골드 가로채 대기).
      // 훈련 전(gold)/후(gold-120-10, upkeep+1)로 첫 대기 건설 완공 턴을 비교.
      auto first_build = [&](long long g0, int extra) -> long long {
        if (parr.empty())
          return 0;
        std::vector<long long> pa = parr;
        std::sort(pa.begin(), pa.end());
        long long g = g0, inc = income, uc = alive + extra;
        for (long long t = 0; t <= MAX_TURN; ++t) {
          if (t > 0)
            g += inc - (long long)UPKEEP_PER_WARRIOR * uc;
          if (g >= (long long)BASE_LEVELS[1].cost && pa[0] <= t)
            return t;
        }
        return MAX_TURN + 1;
      };
      bool delays_pending =
          !parr.empty() && first_build(gold - TRAIN_COST - MOVE_COST, 1) >
                               first_build(gold, 0);
#ifdef DBG_EXP
      std::cerr << "# TRAINCHK T" << turn << " tgt=" << tgt
                << " deliver=" << deliver << " horizon=" << horizon
                << " delays=" << delays_pending
                << " -> " << ((deliver >= horizon && !delays_pending) ? "TRAIN" : "hold")
                << "\n";
#endif
      if (deliver >= horizon && !delays_pending) {
        a.train_n = 1;
        gold -= TRAIN_COST;
        es.jit_target = tgt; // 이 유닛은 다음 턴 이 표적으로(§5 커밋)
      }
    }
  }
#ifdef DBG_EXP
  long long hzn0 = gold_ready(false);
  std::cerr << "T" << turn << " gold=" << S.gold << " hzn=" << hzn0
            << " ctr=" << true_center << " nb=" << (nbase + (int)built.size())
            << " inc=" << income << " P=" << committed.size() << " R=[";
  for (size_t i = 0; i < R.size() && i < 6; ++i)
    std::cerr << R[i].r << "(h" << R[i].h << (R[i].h <= hzn0 ? "IN" : "out")
              << ") ";
  std::cerr << "] -> ";
  for (auto &mv : a.moves)
    std::cerr << "MOVE" << format_warrior(mv.first) << ">" << mv.second << " ";
  if (a.train_n) std::cerr << "TRAIN ";
  for (int u : a.upgrades) std::cerr << "BUILD" << u << " ";
  std::cerr << "\n";
#endif
  return a;
}

int main() {
  GameMap M;
  GameState S;
  parse_init(M, S);             // initialize the game
  Paths P = calculate_paths(M); // calculate the shortest paths

  int turn;
  ExpState es;
  while (read_turn_start(turn)) {
    Actions a = decide(S, M, P, turn, es);
    emit_command();
    emit_actions(a);
    emit_end();
    read_turn_result(S, M, a);
  }
  return 0;
}

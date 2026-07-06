// NEXT NATION — 기능 #2: cascadeRelay
//   = 기능 #1(openingOptimization, 07-06) 그대로 + 캐스케이드 릴레이 이동 스케줄러.
//
// 원리(redesign.md §1(3)·§2, 레퍼리 testing-tool.py 검증 07-06):
//   [경유] 레퍼리는 이동을 가중(유클리드) 최단경로로 걷므로 직행 홉수가 이론 최소가
//   아닐 수 있다. 내 거점 W 경유 분해(h1+h2 <= 직행, ★equal 포함)는 시간 손실 0
//   (t 도착 → t+1 재발령 = 매 아침 끊김 없음) + 정지마다 무료 재결정. 이동비는
//   목적지에 내 건물이면 0이라 내 기지 체인 경유는 골드 중립(최종 레그만 10g).
//   [릴레이] 인바운드 u가 '잔여 1홉'으로 관측되는 커맨드 창에서 W의 보디 L을 u 미션의
//   다음 레그로 출발 → u는 같은 아침 착지·저녁 노동(수입 공백 0), L은 u의 '도착→
//   재발령'보다 1턴 일찍 = 릴레이당 -1턴, 미션 u→L 이관(체인 = 캐스케이드).
// 구성:
//   - RelayTab(사전계산, 게임 시작 시): hop[u][v] = 레퍼리 경로 홉수 N×N — 전
//     거점쌍 경유 가능성의 기하 원천(소유권은 굽지 않음).
//   - 무빙테이블 DP(매 턴, #2b 사용자 07-06): mvt[s][d] = min(직행, h(s,m)+
//     mvt[m][d]−유인크레딧1). 재귀항이 '다중경유'를 열고 크레딧이 파이어(-1)를
//     셈에 넣는다. 이동턴의 단일 소스 — §4 창·§6 JIT·parr 전부 이 수치 참조
//     (무빙턴이 줄면 두 시계·JIT 자동 축소, §1(2) "hops턴 계산은 릴레이 감안").
//   - §1.5 런타임: 미션맵(suffix→최종지) 클린업 → 릴레이 발사 스캔(잔여 1홉) →
//     continuation. relay_dispatch = 모든 장거리 이동의 단일 출구(net argmin;
//     '1홉 유인 링크'는 같은윈도 스왑 = 발령 아침에 그 기지 보디가 다음 레그
//     동시 출발 — 1홉 체인의 -1 회수). §3 커밋 감지는 미션맵 기준 Pass A로
//     교체(경유 시 w.target=경유지라 기존 감지가 놓쳐 이중 파견 나는 것 방지).
// 프로토타입(SampleBot/onlinejudge.cpp) 어긋남 3건 교정: strict '<'로 equal 경유
//   폐기 → '<=', 1홉 인접 스왑 퇴화 → 다홉 인바운드 감시·잔여 1홉 발사, L* 직행
//   홉 선택 → 경유 반영 ETA 선택.
// 수용한 근사(의도): 착지 아침에 적이 같은 아침 유입하면 L은 이미 출발(수입 1턴
//   블립); 미래 최종 레그비 10g은 파견 턴에 로컬 예약만(레그별 실정산); 크레딧
//   경유는 파이어 실패(보디 소진·JIT예약) 시 +1턴 리스크 — 정지 재결정으로 자가
//   복구, 미션 내 재방문 금지로 루프 차단. 이월: 전투/랠리 라우팅(§2 caveat:
//   랠리엔 릴레이 이점 없음), 상대 ETA 경유 반영(금지).
//
// Build: g++ -O2 -std=c++17 -static redesign/cascadeRelay.cpp -o redesign/cascadeRelay.exe
//        (+ -DDBG_RELAY [-DDBG_EXP] → redesign/cr_dbg.exe)
// Test : redesign\run10.ps1 -Exe .\cascadeRelay.exe  (랜덤시드 10판 vs simulator/sample-code.py — 판정은 사용자)

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
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

// ── 기능 #2: 캐스케이드 릴레이 사전계산 (게임 시작 시 1회) ─────────────────────
// hop[u][v] = 레퍼리 실경로(가중 최단, P.nxt) 홉수 = 이동 소요 턴. -1 = 도달불가.
// 이 기하 테이블이 전 거점쌍 경유 가능성 체크의 원천이고, 소유·유인을 얹은
// 런타임 net 이동턴(다중경유·파이어 크레딧 포함)은 decide()의 무빙테이블 DP가
// 매 턴 이 위에서 계산한다(기지는 게임 중 생기고 부서지므로 굽지 않음).
struct RelayTab {
  std::vector<std::vector<int>> hop; // N×N 레퍼리 경로 홉수(-1 도달불가)
  std::vector<int> bnodes;           // 거점 노드 = 요새 ∪ 양 HQ, 정렬 (<=23)
  std::vector<int> bidx;             // node -> bnodes 인덱스, 비거점 = -1
};

static RelayTab build_relay_tables(const GameMap &M, const Paths &P) {
  RelayTab RT;
  const int N = M.N;
  RT.hop.assign(N, std::vector<int>(N, -1));
  // 목적지별 memoized nxt 워크: 한 워크로 경로 위 전 노드의 hop이 확정된다. O(N^2).
  for (int v = 0; v < N; ++v) {
    RT.hop[v][v] = 0;
    for (int u = 0; u < N; ++u) {
      if (RT.hop[u][v] >= 0 || P.nxt[u][v] == -1)
        continue;
      std::vector<int> chain;
      int cur = u;
      while (cur != v && RT.hop[cur][v] < 0 && P.nxt[cur][v] != -1) {
        chain.push_back(cur);
        cur = P.nxt[cur][v];
      }
      if (cur != v && RT.hop[cur][v] < 0)
        continue; // 도달불가 사슬 — -1 유지
      int base = (cur == v) ? 0 : RT.hop[cur][v];
      for (int i = (int)chain.size() - 1; i >= 0; --i)
        RT.hop[chain[i]][v] = base + ((int)chain.size() - i);
    }
  }
  // 거점 = 요새 ∪ 양 HQ (이 봇의 이동 목적지는 항상 거점)
  RT.bnodes = M.strongholds;
  RT.bnodes.push_back(M.my_hq);
  RT.bnodes.push_back(M.opp_hq);
  std::sort(RT.bnodes.begin(), RT.bnodes.end());
  RT.bnodes.erase(std::unique(RT.bnodes.begin(), RT.bnodes.end()),
                  RT.bnodes.end());
  RT.bidx.assign(N, -1);
  for (int i = 0; i < (int)RT.bnodes.size(); ++i)
    RT.bidx[RT.bnodes[i]] = i;
#ifdef DBG_RELAY
  {
    // 기하 요약: 단일 경유 h1+h2 <= 직행(+1)인 거점쌍 수(런타임 DP는 다중경유·
    // 크레딧까지 열므로 실제 가용 범위는 이보다 넓다).
    const int B = (int)RT.bnodes.size();
    int pairs = 0, with_le = 0, best_eq = 0, plus1 = 0, samples = 0;
    for (int si = 0; si < B; ++si)
      for (int di = 0; di < B; ++di) {
        if (si == di)
          continue;
        int s = RT.bnodes[si], d = RT.bnodes[di];
        int direct = RT.hop[s][d];
        if (direct < 0)
          continue;
        ++pairs;
        int best = -1, bm = -1;
        for (int mi = 0; mi < B; ++mi) {
          if (mi == si || mi == di)
            continue;
          int m = RT.bnodes[mi];
          int h1 = RT.hop[s][m], h2 = RT.hop[m][d];
          if (h1 < 1 || h2 < 1)
            continue;
          if (best < 0 || h1 + h2 < best) {
            best = h1 + h2;
            bm = m;
          }
        }
        if (best < 0)
          continue;
        if (best <= direct) {
          ++with_le;
          if (best == direct)
            ++best_eq;
          if (samples < 10) {
            std::cerr << "# RELAYTAB " << s << "->" << bm << "->" << d
                      << " h=" << best << " direct=" << direct << "\n";
            ++samples;
          }
        } else if (best == direct + 1) {
          ++plus1; // 유인 크레딧(-1)으로 net 동률이 되는 쌍
        }
      }
    std::cerr << "# RELAYTAB bases=" << B << " pairs=" << pairs
              << " single_via<=direct=" << with_le << " (equal=" << best_eq
              << ") credit_net_tie=" << plus1 << "\n";
  }
#endif
  return RT;
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
// mission(기능 #2) = 유닛 suffix → 다레그 이동 미션. 경유 라우팅 중엔 w.target이
// 경유지라 최종 목적지는 여기가 유일한 진실. origin/start_turn/direct_h = 최초
// 발령 시점 베이스라인(DONE 로그에서 elapsed vs direct 비교용, 이관 시 보존).
struct Mission {
  int dest = -1;
  int origin = -1;
  int start_turn = 0;
  long long direct_h = 0;
  int fires = 0;            // 릴레이 발사(이관) 누적 — DONE 로그 검증용
  std::vector<int> visited; // 이 미션이 거친 경유 거점(재방문 금지 = 루프 안전장치)
};
struct ExpState {
  int jit_target = -1;
  std::map<int, Mission> mission;
};

// 기능 #1: farthest-in-window + JIT 훈련·출발 (redesign.md §1 두 시계 코어만).
// 기능 #2: + 캐스케이드 릴레이 (§1.5 모듈, RelayTab+무빙테이블 DP, §3 Pass A,
//           §4 창·§6 JIT의 거리시계 = eta_sched).
static Actions decide(const GameState &S, const GameMap &M, const Paths &P,
                      const RelayTab &RT, int turn, ExpState &es) {
  Actions a;
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
  // (기능 #2) 사전계산 테이블 O(1) 룩업 — 의미 동일.
  auto hops = [&](int u, int v) -> long long { return RT.hop[u][v]; };

  // 노동자 파티션: 각 내 건물의 work_cap을 '최저HP 정지 유닛'부터 채운 게 노동자,
  // 나머지 정지 유닛 = 잉여(정착민 후보). 지역 유닛수>cap일 때 전원을 잉여로 보던
  // 버그 방지 — 반드시 딱 cap기만 노동자로 남기고 나머지만 뺀다.
  // (기능 #2) 미션 캐리어(경유 대기 중)는 떠날 몸이라 노동자로 세지 않는다.
  std::set<int> worker;
  for (const auto &b : S.buildings) {
    if (b.side != me)
      continue;
    std::vector<const Warrior *> on;
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == b.region &&
          w.state == WState::STATIONARY && !es.mission.count(w.id.num))
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

  // ── 1.5) 캐스케이드 릴레이 모듈 (기능 #2) ──────────────────────────────────
  // §1 뒤 배치: §1은 빈 요새 위 유닛만 소비(릴레이 참가자 풀과 분리), 골드 우선
  // 순위 300건설 > 10이동비 유지, §1의 built/used를 캡처. §3~§6보다는 앞(발사·
  // continuation의 used/mission 결과를 커밋·파견이 봐야 함).
  auto my_building_at = [&](int r) -> const Building * {
    const Building *b = building_at(r);
    return (b && b->side == me) ? b : nullptr;
  };
  // 릴레이 보디로 쓸 수 있는 정지 유닛 존재 여부(경유 선택의 '유인' 선호 판정)
  auto manned = [&](int r) -> bool {
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == r &&
          w.state == WState::STATIONARY && !es.mission.count(w.id.num))
        return true;
    return false;
  };
  auto my_warrior_by_num = [&](int num) -> const Warrior * {
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.id.num == num)
        return &w;
    return nullptr;
  };
  // (기능 #2b, 사용자 07-06) 무빙테이블 DP — 이동턴의 단일 소스. RelayTab 기하
  // 위에 현 소유·유인을 얹어 net 이동턴을 만들고, JIT·창·parr 등 소비자는 전부
  // 이 수치를 참조한다(스케줄러가 턴을 줄이면 두 시계·JIT가 자동으로 따라 축소).
  //   mvt[s][d] = min( hop[s][d],  min over 내 소유 거점 m:
  //                    hop[s][m] + mvt[m][d] − (유인 m ? 1 : 0) )
  // 재귀항 mvt[m][d]가 '다중경유'를 연다(잔여구간을 직행으로만 보던 단일경유
  // 한계 제거). 유인 크레딧 -1 = 그 기지 파이어(보디 조기출발) 기대값 — 유인
  // 1홉 링크는 net +0이라 §1(3) '1홉 체인'이 이 식의 극한으로 재현된다. 크레딧은
  // 파이어 실패(보디 소진 등) 시 +1 리스크 — 정지마다 재결정으로 자가복구.
  const int NB = (int)RT.bnodes.size();
  std::vector<char> ownedB(NB, 0), mannedB(NB, 0);
  for (int i = 0; i < NB; ++i) {
    ownedB[i] = my_building_at(RT.bnodes[i]) ? 1 : 0;
    mannedB[i] = (ownedB[i] && manned(RT.bnodes[i])) ? 1 : 0;
  }
  // blocked[i]=1 거점을 중간 경유로 금지한 조건부 테이블(Bellman 고정점, 링크
  // net비용 >= 0 → 수렴). 미션 라우팅은 visited∪{현위치}를 막고 재계산한다 —
  // 지나온 기지로 '되돌아가는 크레딧'이 값을 부풀려 약속(-1)을 못 지키는 바운스
  // (예: 0→6→0→d) 차단. 전역 테이블(무마스크)은 계획 소비자용.
  auto run_mvt = [&](const std::vector<char> &blocked) {
    std::vector<std::vector<long long>> t(NB, std::vector<long long>(NB));
    for (int si = 0; si < NB; ++si)
      for (int di = 0; di < NB; ++di)
        t[si][di] = RT.hop[RT.bnodes[si]][RT.bnodes[di]];
    for (bool chg = true; chg;) {
      chg = false;
      for (int si = 0; si < NB; ++si)
        for (int di = 0; di < NB; ++di) {
          if (si == di || t[si][di] < 0)
            continue;
          for (int mi = 0; mi < NB; ++mi) {
            if (mi == si || mi == di || !ownedB[mi] || blocked[mi] ||
                t[mi][di] < 0)
              continue;
            int h1 = RT.hop[RT.bnodes[si]][RT.bnodes[mi]];
            if (h1 < 1)
              continue;
            long long val = h1 + t[mi][di] - (mannedB[mi] ? 1 : 0);
            if (val < t[si][di]) {
              t[si][di] = val;
              chg = true;
            }
          }
        }
    }
    return t;
  };
  std::vector<std::vector<long long>> mvt = run_mvt(std::vector<char>(NB, 0));
  // 스케줄러 반영 ETA(내 이동 전용 — 상대 예측은 §2 비대칭 원칙상 직행 hops 유지).
  auto eta_sched = [&](int s, int d) -> long long {
    if (s == d)
      return 0;
    int si = RT.bidx[s], di = RT.bidx[d];
    if (si >= 0 && di >= 0)
      return mvt[si][di];
    return hops(s, d); // 비거점 출발 방어 폴백
  };
  struct DispatchRes {
    long long eta; // 계획 net ETA(무빙테이블, 발령 시점 위치 기준). <0 = 발령 안 함
    int fee;       // 이번 윈도에 실제 지불한 이동비 합(0 = 전 레그가 내 소유지)
  };
  // W의 릴레이 보디 선택: 내 편·정지·미used·무미션. JIT 커밋 중엔 HQ에 1기 예약
  // (HQ 보디는 위치상 등가 — 수만 예약하면 §5 head가 남는 몸으로 이행). 선호:
  // 비노동 잉여(노동 무교란) → 노동자 최고HP → 작은 suffix.
  auto pick_relay_body = [&](int W) -> const Warrior * {
    std::vector<const Warrior *> cand;
    for (const auto &l : S.warriors)
      if (l.id.side == me && l.hp > 0 && l.region == W &&
          l.state == WState::STATIONARY && !es.mission.count(l.id.num) &&
          std::find(used.begin(), used.end(), l.id.num) == used.end())
        cand.push_back(&l);
    if (cand.empty())
      return nullptr;
    if (W == M.my_hq && es.jit_target >= 0 && (int)cand.size() < 2)
      return nullptr;
    std::sort(cand.begin(), cand.end(),
              [&](const Warrior *x, const Warrior *y) {
                bool xw = worker.count(x->id.num) > 0;
                bool yw = worker.count(y->id.num) > 0;
                if (xw != yw)
                  return !xw;
                if (x->hp != y->hp)
                  return x->hp > y->hp;
                return x->id.num < y->id.num;
              });
    return cand.front();
  };
  // relay_dispatch = 모든 장거리 이동의 단일 출구. 첫 레그 = 무빙테이블과 정합한
  // argmin: net(= h1 + mvt[m][dest] − 유인크레딧) 최소 → 경유>직행(동률 = 정지·
  // 릴레이 가치) → 유인 → h1 최소 → 작은 id. net <= 직행만 채택(다중경유·크레딧
  // 포함). 첫 레그가 '1홉 유인 기지'면 같은윈도 스왑 루프: w는 이번 아침 착지
  // (노동 백필), 그 기지 보디가 다음 레그를 같은 아침 출발(잔여1홉 룰의 h1==1
  // 특수해 — 없으면 1홉 링크의 -1이 전부 샌다). 미션은 최종 캐리어가 보유.
  auto relay_dispatch = [&](const Warrior *w0, int dest) -> DispatchRes {
    if (!w0 || w0->state != WState::STATIONARY || w0->region == dest)
      return {-1, 0};
    if (std::find(used.begin(), used.end(), w0->id.num) != used.end())
      return {-1, 0};
    if (hops(w0->region, dest) < 0)
      return {-1, 0};
    long long promise = -1; // 첫 레그 선택 시 확정(마스킹 반영 정직값)
    Mission ms;
    auto mit0 = es.mission.find(w0->id.num);
    if (mit0 != es.mission.end() && mit0->second.dest == dest)
      ms = mit0->second; // continuation/이관 — 베이스라인·visited·fires 보존
    else {
      ms.dest = dest;
      ms.origin = w0->region;
      ms.start_turn = turn;
      ms.direct_h = hops(w0->region, dest);
    }
    const Warrior *w = w0;
    int fee_sum = 0;
    bool issued = false;
    for (;;) {
      int s0 = w->region;
      long long direct = hops(s0, dest);
      long long bnet = direct, brem = -1;
      int bkind = 2, bfirst = dest; // kind: 0=유인 경유, 1=경유, 2=직행
      int si = RT.bidx[s0], di = RT.bidx[dest];
      if (si >= 0 && di >= 0) {
        // 미션별 마스킹: visited∪{현위치}를 중간경유 금지로 재계산 — 되돌아가기
        // 크레딧으로 부푼 전역값 대신 이 미션이 실제 달성 가능한 정직한 값.
        std::vector<char> blocked(NB, 0);
        blocked[si] = 1;
        for (int vb : ms.visited)
          if (RT.bidx[vb] >= 0)
            blocked[RT.bidx[vb]] = 1;
        std::vector<std::vector<long long>> mv2 = run_mvt(blocked);
        for (int mi = 0; mi < NB; ++mi) {
          if (mi == si || mi == di || !ownedB[mi] || blocked[mi] ||
              mv2[mi][di] < 0)
            continue;
          int m = RT.bnodes[mi];
          if (enemy_warrior_at(m))
            continue; // 적 동거 경유지 = 정지 시 핀 → 제외(라이브 체크)
          long long h1 = RT.hop[s0][m];
          if (h1 < 1)
            continue;
          int cr = mannedB[mi] ? 1 : 0;
          long long rem = mv2[mi][di];
          long long val = h1 + rem - cr;
          if (val > direct)
            continue; // net이 직행 초과면 제외(동률은 정지·릴레이 가치로 채택)
          int kind = cr ? 0 : 1;
          bool better = false;
          if (val != bnet)
            better = val < bnet;
          else if (kind != bkind)
            better = kind < bkind;
          else if (brem >= 0 && rem != brem)
            better = rem < brem; // 동률은 전방 진행(도착 근접 정지) 우선
          if (better) {
            bnet = val;
            bkind = kind;
            bfirst = m;
            brem = rem;
          }
        }
      }
      if (promise < 0)
        promise = bnet;
      int fee = my_building_at(bfirst) ? 0 : MOVE_COST; // read_turn_result 동일 모델
      if (gold < fee) {
        if (issued)
          es.mission[w->id.num] = ms; // 스왑 도중 골드 소진 — 다음 창 continuation
        break;
      }
      gold -= fee;
      fee_sum += fee;
      a.moves.emplace_back(w->id, bfirst);
      used.push_back(w->id.num);
      issued = true;
#ifdef DBG_RELAY
      std::cerr << "# T" << turn << " RDISP " << format_warrior(w->id) << " "
                << s0 << "->" << dest << " direct=" << direct;
      if (bfirst != dest)
        std::cerr << " via=" << bfirst << " net=" << bnet
                  << " manned=" << (bkind == 0 ? 1 : 0);
      std::cerr << " fee=" << fee << "\n";
#endif
      if (bfirst == dest) {
        es.mission[w->id.num] = ms;
        break;
      }
      ms.visited.push_back(bfirst);
      const Warrior *L = nullptr;
      if (hops(s0, bfirst) == 1 && !enemy_warrior_at(bfirst) &&
          !enemy_warrior_at(s0))
        L = pick_relay_body(bfirst);
      if (!L) {
        es.mission[w->id.num] = ms; // w가 캐리어 지속(다홉 링크는 반응 스캔 담당)
        break;
      }
      es.mission.erase(w->id.num); // w는 bfirst에 착지·노동 백필(미션 없는 주민)
      worker.erase(L->id.num);     // L이 노동자였다면 이번 턴 집계에서 제외
      ms.fires++;
#ifdef DBG_RELAY
      std::cerr << "# T" << turn << " FIRE0 W=" << bfirst << " in="
                << format_warrior(w->id) << " L=" << format_warrior(L->id)
                << " dest=" << dest << " (-1 same-window)\n";
#endif
      w = L;
    }
    if (!issued)
      return {-1, 0};
    return {promise, fee_sum};
  };

  // (a) 미션 클린업 — 발사·continuation보다 반드시 먼저.
  for (auto it = es.mission.begin(); it != es.mission.end();) {
    const Warrior *w = my_warrior_by_num(it->first);
    bool drop = false;
    if (!w || w->hp <= 0) {
      drop = true; // ① 사망(레퍼리 결과에서 이미 제거됨)
    } else if (w->state == WState::STATIONARY &&
               w->region == it->second.dest) {
#ifdef DBG_RELAY
      std::cerr << "# T" << turn << " DONE " << format_warrior(w->id)
                << " dest=" << it->second.dest
                << " elapsed=" << (turn - it->second.start_turn)
                << " direct=" << it->second.direct_h
                << " fires=" << it->second.fires
                << " origin=" << it->second.origin << "\n";
#endif
      drop = true; // ② 최종지 도착(elapsed<direct = 릴레이 승, == = 정지만)
    } else if (std::find(used.begin(), used.end(), it->first) != used.end()) {
      drop = true; // ③ §1이 이 몸으로 건설(경유지 파괴→빈 요새 착지 케이스) = 건설 승
    } else {
      int d = it->second.dest;
      if (!is_stronghold(d) || building_at(d) || enemy_warrior_at(d))
        drop = true; // ④ 표적 무효화 → 캐리어는 잉여 복귀(정지 = 무료 재결정 지점)
    }
    if (drop)
      it = es.mission.erase(it);
    else
      ++it;
  }

  // (b) 릴레이 발사 스캔: 인바운드 u가 '잔여 1홉'(다음 아침 착지 확정) → W의 보디
  // L을 u 미션의 다음 레그로 지금 출발. 같은 아침에 u 착지(노동 백필) + L 첫 홉 =
  // 수입 공백 0, L은 u의 '도착→재발령' 대비 1턴 선행 = 릴레이당 -1턴.
  for (const auto &w : S.warriors) {
    if (w.id.side != me || w.hp <= 0 || w.state != WState::MOVING)
      continue;
    auto mit = es.mission.find(w.id.num);
    if (mit == es.mission.end())
      continue;
    int W = w.target; // MOVING일 때만 유효한 필드
    if (W == mit->second.dest)
      continue; // 최종 레그엔 릴레이 없음(그 도착이 곧 미션 완료)
    if (!my_building_at(W))
      continue; // 발사 시점 재확인(경유지 비행 중 파괴 커버)
    if (hops(w.region, W) != 1)
      continue; // 사용자 확정: 잔여 1홉 관측 시에만(수입 공백 0)
    if (enemy_warrior_at(W) || enemy_warrior_at(w.region))
      continue; // 양단 핀 리스크 — 발사 보류(u 착지 후 continuation 폴백)
    const Warrior *L = pick_relay_body(W);
    if (!L)
      continue;
    Mission carry = mit->second;
    carry.visited.push_back(W);
    carry.fires++;
    es.mission[L->id.num] = carry; // 사전 이관(경로선택이 visited·베이스라인을 보게)
    DispatchRes r = relay_dispatch(L, carry.dest);
    if (r.eta < 0) {
      es.mission.erase(L->id.num); // 롤백 — 이관 없음, u 착지 후 continuation 폴백
      continue;
    }
    es.mission.erase(w.id.num); // u는 W에 착지해 노동 백필(미션 없는 주민)
    worker.erase(L->id.num);    // L이 노동자였다면 이번 턴 집계에서 제외
#ifdef DBG_RELAY
    std::cerr << "# T" << turn << " FIRE W=" << W << " in="
              << format_warrior(w.id) << "@" << w.region
              << " L=" << format_warrior(L->id) << " dest=" << carry.dest
              << " (-1)\n";
#endif
  }

  // (c) continuation: 경유지 도착 캐리어의 다음 레그 재분해 재발령(정지 손실 0).
  // 클린업 뒤라 dest 유효·미도착 보장. 골드 부족이면 미션 유지(§3 Pass A가 잠금).
  for (const auto &w : S.warriors) {
    if (w.id.side != me || w.hp <= 0 || w.state != WState::STATIONARY)
      continue;
    auto mit = es.mission.find(w.id.num);
    if (mit == es.mission.end() || w.region == mit->second.dest)
      continue;
    if (std::find(used.begin(), used.end(), w.id.num) != used.end())
      continue;
    if (enemy_warrior_at(w.region))
      continue; // 핀 — 이동 잠금 리스크라 이번 턴 보류
    relay_dispatch(&w, mit->second.dest);
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
  // Pass A(기능 #2): 미션 캐리어의 커밋 진실은 w.target(경유 중엔 경유지)이 아니라
  // 미션맵. 이게 없으면 경유 라우팅 시 커밋을 놓쳐 동일 요새 이중 파견이 난다.
  // 클린업 직후라 dest는 유효한 빈 요새(당턴 built만 별도 배제).
  for (const auto &pr : es.mission) {
    const Warrior *w = my_warrior_by_num(pr.first);
    if (!w)
      continue;
    int d = pr.second.dest;
    if (std::find(built.begin(), built.end(), d) != built.end())
      continue; // 당턴 타인 건설 — 다음 턴 클린업 ④가 미션 정리
    committed.push_back(d);
    long long rem = (w->state == WState::MOVING)
                        ? hops(w->region, w->target) + eta_sched(w->target, d)
                        : eta_sched(w->region, d); // 경유지 대기(발령분 포함)
    parr.push_back(std::max(0LL, rem));
    if (w->state == WState::STATIONARY &&
        std::find(used.begin(), used.end(), w->id.num) == used.end())
      used.push_back(w->id.num); // 골드 대기 캐리어 잠금(아래 대기 정착민 고정과 동형)
  }
  // Pass B: 미션 없는 케이스(최종지 도착·대기 정착민 등) — 기존 로직 그대로.
  for (const auto &w : S.warriors) {
    if (w.id.side != me)
      continue;
    if (std::find(used.begin(), used.end(), w.id.num) != used.end())
      continue; // 이번 턴 건설 완료분
    if (es.mission.count(w.id.num))
      continue; // (기능 #2) 미션 캐리어는 Pass A 전담 — w.target은 경유지일 수 있음
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
    // (기능 #2b) 거리 시계 = 무빙테이블 net 턴(다중경유·릴레이 크레딧 반영) —
    // 무빙턴이 줄면 창(farthest-in-window)이 자동으로 더 깊은 표적을 연다.
    long long h = eta_sched(M.my_hq, sh);
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
  // 부족이면 false. (기능 #2) 발령은 relay_dispatch 단일 출구(경유 라우팅+미션 기록),
  // 몸 선택 척도도 경유 반영 ETA — 프로토타입의 '직행 홉 L* 선택' 어긋남 교정.
  auto dispatch_to = [&](int tgt) -> bool {
    const Warrior *body = nullptr;
    long long best = -1;
    for (const auto &w : S.warriors) {
      if (w.id.side != me || w.state != WState::STATIONARY)
        continue;
      if (worker.count(w.id.num))
        continue; // 노동자(수입 기여) = 잉여 아님
      if (es.mission.count(w.id.num))
        continue; // (기능 #2) 미션 캐리어는 릴레이 모듈 전담
      if (std::find(used.begin(), used.end(), w.id.num) != used.end())
        continue;
      if (enemy_warrior_at(w.region))
        continue; // 적 동거 = 핀
      long long h = eta_sched(w.region, tgt);
      if (h < 0)
        continue;
      if (body == nullptr || h < best) {
        body = &w;
        best = h;
      }
    }
    if (body == nullptr || gold < MOVE_COST)
      return false;
    DispatchRes r = relay_dispatch(body, tgt);
    if (r.eta < 0)
      return false;
    if (r.fee == 0)
      gold -= MOVE_COST; // 소유 경유 레그 = 0비용이어도 최종 레그비 10g 로컬 예약
                         // (§6 훈련 게이트 보수성 유지 — 실제 현금흐름은 레그별 정산)
    parr.push_back(r.eta); // 도착 = 이 정착민 건설 예정 (다음 슬롯 horizon 확장)
    taken.insert(tgt);
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
      // (기능 #2) 도착 홉수 = 경유 반영 ETA (§1(2) "hops턴 계산은 캐스케이드
      // 릴레이를 감안" — 더 빨리 닿으면 더 늦게 뽑아도 됨 = upkeep 누수 감소).
      long long deliver = eta_sched(M.my_hq, tgt) + 1;
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
  RelayTab RT = build_relay_tables(M, P); // (기능 #2) 홉·경유 테이블 사전계산

  int turn;
  ExpState es;
  while (read_turn_start(turn)) {
    Actions a = decide(S, M, P, RT, turn, es);
    emit_command();
    emit_actions(a);
    emit_end();
    read_turn_result(S, M, a);
  }
  return 0;
}

// NEXT NATION — goldenhammer + centerRelapse 프로토타입 (07-07, 사용자 판정 10건 반영).
// 베이스 = goldenhammer.cpp(clean-room rebuild) 그대로, 확장/중앙 스케줄링만
// redesign/centerRelapse.cpp(#2 무빙테이블 + #4 HQ스와프 + #5 창판정·경합회귀) 체계로 교체.
//
// 연결점 판정(사용자 07-07, 하나씩 문답):
//   ①이식 범위 = #2 무빙테이블까지 전부  ②정착·확장 이동 = relay_dispatch 단일 출구
//     (기존 노동자 백필 캐스케이드·1턴 이득 게이트·정착용 issue_route 삭제; 공세/수비/
//     회수 이동은 기존 issue_move/issue_attack/relay_pick 유지)
//   ③중앙 스택 = 중앙만 committed 중복금지·max_inflight 면제, 무제한
//   ④중앙 창 판정 = centerRelapse 골드슬롯 기준으로 교체(기존 2·3번째 하드코딩 제거)
//   ⑤훈련 우선순위 = 수비 비상 > 중앙 JIT > 상비군
//   ⑥수비 need-bound 징집은 중앙행 정지 몸 회수 허용(생존 최우선; 미션은 클린업이
//     자연 정리, 스티키 회귀가 재파견)
//   ⑦#4 스와프 게이트 = centerRelapse 기준(적 HQ 동거 시에만 금지)
//   ⑧doomed 밥값 제외 = 중앙 스케줄링 계산에만(그 국면엔 다른 경제요소 없음)
//   ⑨경합 전쟁(스티키) 중 잉여 = 전부 중앙(비중앙 확장·공세 웨이브·랠리 정지)
//   ⑩경합 전쟁 중 경제 = 전쟁 전용 — HQ/기지 업글·상비군 등 재량 지출 전면 동결
//     (수비 비상훈련만 예외: 다른 데 돈 쓰면 경합전쟁 짐)
//
// homeHalf(기능 #6, 07-07 이식): §4 후보 R 구성에서 상대쪽(doo < α·dm) 거점 제외 —
//   farthest-in-window가 상대진영 깊숙이 지으면 상대 증원 거리가 짧아 금방 뺏긴다.
//   α=0.90(중앙선 거의 등거리까지만; homeHalf α 이력 1.0→0.75→0.45→0.90). 척도는
//   true_center 선정과 동일(가중거리 P.dist, 상대 도달불가는 우리쪽 취급). 정수 백분율.
//
// rushguard v2(하드코딩, 07-07 사용자 재정리): 221508_1P·221963_1P 매스러시 전담 수비.
//   상대가 초반에 5기를 한 덩어리로 내 HQ에 직행시키면 기존 need-bound 수비는 병력을
//   길목에 축차 투입해 각개격파당하고 HQ가 뚫린다(두 로그 모두 HQ_DESTROYED 패).
//   ① 감지 = 10턴 이내, '상대 HQ를 포함한' 아무 구역에 상대 유닛 5기+ 뭉침
//      (출격 전 스테이징 단계부터 조기 감지; 관측은 결과블록 = 전체 정보)
//   ② 감지 즉시 두 번째 건물 스케줄링 취소(정착·중앙·공세 커밋 파기). '해당 유닛만'
//      = 내 건물 밖 떠돌이(정착민)만 첫 번째로 지은 기지로 회수. 노동자는 제자리 근무,
//      잉여 훈련·업글도 동결 = 골드 뱅킹(격퇴 직후 재건 밑천).
//   ③ 상대 매스가 내 HQ '한 칸 앞'(인접 구역 or HQ 위)에 도달한 순간부터: 매턴 계속
//      훈련(골드/생산캡 한도) + 첫 기지에 있던 유닛(최대 2기) HQ 소환. 이동·스폰·전투가
//      같은 날 처리라 같은 턴 전투부터 합류. 공성당하는 동안도 생산 지속.
//   ④ 해제 = '우리한테 들어왔던 유닛이 다 갈린 직후'의 문자적 구현(침입자 명부):
//      에피소드 중 위협권(내 HQ쪽 전진 or HQ 6턴권 or 우리 절반)에 든 상대 suffix를
//      명부에 누적 — 명부 생존자 0 & 상대 HQ 스테이징 4기 미만이면 즉시 원래
//      확장기지최적화 스케줄링 모드 복귀(미션은 파이프라인이 매턴 재산출). 트리클도
//      출발(전진) 순간 명부에 올라 웨이브 사이 빈틈(HQ 앞 0기)에 풀리지 않는다.
//      자기 진영으로 회군해 눌러앉은 유닛은 명부 말소(영구 가드 방지).
//   ⑤ 첫 기지는 무조건: 매스 감지와 무관. 가드 중에도 기지가 없으면 집결지 = 첫 기지
//      자리(우리쪽 최근접 빈 거점), 정지 유닛이 그 위에 서면 즉시 건설.
//   ⑥ 러시로 발동됐을 상비군 조기개방(econ_open의 rush/열세/브레이싱 트리거)은 교전
//      에피소드 종료와 함께 다시 꺼짐 — 이후는 정상 개방 조건(우리쪽 거점 완비)만.
//   ⑦ 격퇴 원상복구: 해제 턴에 HQ 생존자(저HP 우선)를 첫 기지 work_cap 부족분만큼
//      되돌림 — ③의 소환으로 기지가 빈 채 방치되는 것 방지. 기지 우선(HQ 노동
//      슬롯은 신병 스폰이 자연 백필, 기지는 채울 주체 없음 — ② 백필 삭제).
//   비러시 오발(초반 훈련병이 잠시 HQ에 5기 뭉침 등)은 해제 조건이 다음 턴 자동 정리
//   (확장 1~2턴 지연 비용). 이동 중(MOVING) 유닛은 재명령 불가(즉시패 규칙) → 도착 후
//   다음 턴 회수.
//
// Build:
//   g++ -O2 -std=c++17 SampleBot/goldenhammer_crl_rushguard.cpp -o SampleBot/goldenhammer_crl_rushguard.exe
//   (+ -DSIM_TEST / -DOBS_TEST 게이트 동일, + -DBOTDBG 결정층 디버그)
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

#ifdef EVAL_OVERCOMMIT
// --- conditional overcommit (econ-lead-gated decisive assault) tunables ---
// 값은 gym 스윕으로 확정 예정. 초기 후보.
[[maybe_unused]] constexpr long long OC_GOLD_FLOOR   = 1500; // 유휴금화 버퍼 하한(수비훈련+식비 런웨이)
[[maybe_unused]] constexpr int       OC_STRIKE_MARGIN = 3;   // hq_clear 위 결정타 여유
[[maybe_unused]] constexpr int       OC_MIN_SIEGE     = 3;   // 도착 후 최소 공성 턴 여유
#endif

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
static void apply_upgrades(SimState &st, [[maybe_unused]] const GameMap &M,
                           Side s, const Cmds &c, ResultBlock &rb) {
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

static void apply_moves(SimState &st, [[maybe_unused]] const GameMap &M, Side s, const Cmds &c) {
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
static void apply_day_movement(SimState &st, [[maybe_unused]] const GameMap &M,
                               const Paths &P, ResultBlock &rb) {
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

static void spawn_trained(SimState &st, [[maybe_unused]] const GameMap &M,
                          Side s, int n, ResultBlock &rb) {
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

static void apply_day_combat(SimState &st, [[maybe_unused]] const GameMap &M,
                             ResultBlock &rb,
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
[[maybe_unused]] static std::vector<std::string> render_result(ResultBlock rb) {
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
[[maybe_unused]] static std::vector<std::string> to_send_block(const ResultBlock &rb, int day) {
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

#ifdef EVAL_OVERCOMMIT
// 확장 국면이 끝났는가: 내가 도달 가능한 빈(미건설) 거점이 하나도 없으면 true.
// 실측(2026-07-06) 2차 수정 — "누구 소유든 빈 거점이 없다"도 사실상 영원히 거짓임을 확인:
// 지도엔 어느 쪽도 닿지 못하는(고립/비효율) 거점이 항상 남아 영구히 미건설 상태. 의도
// ("내가 더 지을 확장 여지 없음")에 맞게 "내 HQ에서 도달 가능한 거점 중 빈 곳이 없다"로
// 재정의 — 도달 불가 거점은 애초에 내 선택지가 아니므로 판정에서 제외.
static bool oc_all_strongholds_built(const SimState &S, const GameMap &M, const Paths &P, int my_hq) {
  for (int r : M.strongholds) {
    if (!building_at(S, r) && reachable(P, my_hq, r)) return false; // 내가 닿을 수 있는 빈 거점 → 아직 확장 여지
  }
  return true;
}
// 경제 포화: HQ가 level_floor 이상 + 확장 완료(inflight 0) [+ require_sh면 내 도달권
// 거점 건설 완료]. level_floor/require_sh 기본값=HQ_MAX_LEVEL/true(원안 그대로).
// 실측(72판 self-play, 2026-07-06, 3회 반복): (1) "모든 거점 내 소유"는 2인 대전에서
// 상대가 항상 일부 차지 → 영원히 거짓. (2) "누구 소유든 빈 거점 없음"도 고립/경합
// 지역이 영구 미건설로 남아 여전히 거짓. (3) "내 도달권 빈 거점 없음"도 200턴 내
// 경합 거점이 항상 남아 여전히 거짓 — 3회 모두 게이트 미발동(INERT). goldenhammer.cpp
// (완화판)는 호출부에서 level_floor=HQ_MAX_LEVEL-1 & require_sh=false로 실전에서
// 도달 가능한 조건으로 완화(경제 정체+유휴금화만으로 판정, 거점 완결 불요구). 원안
// (HQ만렙+거점 완결 엄수)은 goldenhammer_HQ5.cpp에 보존.
static bool oc_saturated(const SimState &S, const GameMap &M, const Paths &P, int my_hq,
                         int hq_level, int inflight, int level_floor = HQ_MAX_LEVEL,
                         bool require_sh = true) {
  if (hq_level < level_floor || inflight != 0) return false;
  return !require_sh || oc_all_strongholds_built(S, M, P, my_hq);
}
// 내 HQ에 도달 가능한 상대 hp>0 전사들의 hp 목록. (M은 시그니처 일관성용 — 미사용)
static std::vector<int> oc_reachable_enemy_hps(const SimState &S, [[maybe_unused]] const GameMap &M,
                                               const Paths &P, Side opp, int my_hq) {
  std::vector<int> hps;
  for (const auto &w : S.warriors)
    if (w.id.side == opp && w.hp > 0 && reachable(P, w.region, my_hq)) hps.push_back(w.hp);
  return hps;
}
// 홈 수비 불변식(핵심): 내 HQ 도달가능 상대군 + 행군중 재훈련 추정(march_retrain명, enemy_whp)에
// 맞서 내 HQ를 지킬(공격자 전멸) 최소 수비수 수. (소비처는 T5 — 그전까지 [[maybe_unused]])
[[maybe_unused]] static int oc_counter_need(const SimState &S, const GameMap &M, const Paths &P,
                                            Side opp, int my_hq, int my_whp, int hq_turret,
                                            int enemy_whp, int march_retrain) {
  std::vector<int> hps = oc_reachable_enemy_hps(S, M, P, opp, my_hq);
  for (int i = 0; i < march_retrain; ++i) hps.push_back(enemy_whp > 0 ? enemy_whp : my_whp);
  return min_defenders_to_hold(hps, my_whp, hq_turret, 64);
}
// 타이밍 성립: 빌드업 + 행군ETA + 최소공성 여유가 남은 턴(MAX_TURN-turn) 안에 드는가.
// (march_eta==PINF = 도달 불가 → false)
[[maybe_unused]] static bool oc_timing_ok(int turn, long long march_eta, int buildup_turns_est) {
  if (march_eta >= PINF) return false;
  return (long long)buildup_turns_est + march_eta + OC_MIN_SIEGE <= (long long)(MAX_TURN - turn);
}
// 결진 규모: 상대 HQ 현 수비(ohq_def, ohq_turret) 격파 최소 병력 + 마진.
[[maybe_unused]] static int oc_assault_target(int my_whp, const std::vector<int> &ohq_def,
                                              int ohq_turret, int margin) {
  return min_mass_to_take(my_whp, ohq_def, ohq_turret, 64) + margin;
}
#endif

// ================================ contest I/O ================================
#if !defined(SIM_TEST) && !defined(OBS_TEST) && !defined(UNIT_TEST)

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
#ifdef CRLDBG
// 릴레이/전쟁 검증 프린트는 '라이브' decide에서만(롤아웃 64×/턴 호출 스팸 방지).
static bool g_crl_live = false;
#endif
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
  // --- centerRelapse 이식 상태 ---
  struct Mission { // 정착민 다레그 이동 미션(경유 중엔 w.target=경유지라 최종지는 여기가 진실)
    int dest = -1, origin = -1, start_turn = 0;
    long long direct_h = 0;
    long long promise = -1; // 최초 발령 시점의 계획 net ETA — 감사 불변식: elapsed==promise
                            // (직행 홉수 direct_h가 아님: 경유 분해가 가중경로의 홉 낭비를
                            // 줄여 fires와 별개로 직행보다 빨라질 수 있음 — #2 원천 통찰)
    int fires = 0;
    std::vector<int> visited; // 재방문 금지(루프 차단)
  };
  std::map<int, Mission> mission; // 내 유닛 suffix -> 미션
  int jit_target = -1;            // 지난 턴 JIT 훈련 유닛의 커밋 표적
  bool center_war = false;        // 경합 확정 스티키(중앙 완공까지 유지)
  bool center_ever_built = false; // 중앙에 건물이 지어진 이력(진영 무관) — 이후 war 영구 금지
  // --- rushguard(하드코딩) 상태 ---
  bool rush_guard = false;   // 초반 5기 뭉침 감지 스티키(매스 소멸 시 해제)
  int rush_rally = -1;       // 첫 번째로 지은 기지(회수 집결지); 소실 시 HQ 폴백
  bool rush_engaged = false; // ③ 매스 교전 개시(HQ 한 칸 앞 도달) 스티키 — 이후 매턴 훈련
  bool rush_done = false;    // ⑥ 교전 에피소드 완료 — 상비군 조기개방 재점화 금지
  std::set<int> rush_intruders; // ④ 침입자 명부: 에피소드 중 위협권에 든 상대 suffix
#ifdef EVAL_OVERCOMMIT
  int oc_phase = 0;    // 0=NONE, 1=BUILDUP, 2=LAUNCHED
  int oc_target = -1;  // 오버커밋 표적(상대 HQ 구역)
#endif
  std::map<int, int> route_to;       // 내 유닛 suffix -> 최종 목적지 (기지 경유 라우팅 중; inflight 추적용)
  int dbg_thr = 0, dbg_att = 0, dbg_inhq = 0, dbg_rush = 0; // BOTDBG 계측 (live 호출이 마지막)
};

// HP-aware partition: fill each building's work_cap with the LOWEST-hp stationary
// units (군 체력=재생불가 자원 -> 저HP는 노동, 고HP는 전투 보존). Rest = mobile.
// (centerRelapse #2) 미션 캐리어(경유 대기 중)는 떠날 몸이라 노동자로 세지 않는다.
static std::vector<const Warrior *> partition_units(const SimState &S, const GameMap &M,
                                                    std::set<int> &lab,
                                                    const std::map<int, BotState::Mission> &mission) {
  Side me = M.my_side;
  lab.clear();
  for (const auto &b : S.buildings) {
    if (b.side != me) continue;
    std::vector<const Warrior *> on;
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == b.region && w.state == WState::STATIONARY &&
          !mission.count(w.id.num))
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

// ===== centerRelapse #2 이식: 릴레이 사전계산 (게임 시작 시 1회) ================
// hop[u][v] = 레퍼리 실경로(P.nxt) 홉수 = 이동 소요 턴(-1 도달불가). 전 거점쌍
// 경유 가능성의 기하 원천 — 런타임 무빙테이블(run_mvt)이 소유·유인을 얹는다.
struct RelayTab {
  std::vector<std::vector<int>> hop;
  std::vector<int> bnodes; // 거점 = 요새 ∪ 양 HQ, 정렬
  std::vector<int> bidx;   // node -> bnodes 인덱스, 비거점 = -1
};
static RelayTab build_relay_tables(const GameMap &M, const Paths &P) {
  RelayTab RT;
  const int N = M.N;
  RT.hop.assign(N, std::vector<int>(N, -1));
  for (int v = 0; v < N; ++v) { // 목적지별 memoized nxt 워크 O(N^2)
    RT.hop[v][v] = 0;
    for (int u = 0; u < N; ++u) {
      if (RT.hop[u][v] >= 0 || P.nxt[u][v] == -1) continue;
      std::vector<int> chain;
      int cur = u;
      while (cur != v && RT.hop[cur][v] < 0 && P.nxt[cur][v] != -1) {
        chain.push_back(cur);
        cur = P.nxt[cur][v];
      }
      if (cur != v && RT.hop[cur][v] < 0) continue; // 도달불가 사슬
      int base = (cur == v) ? 0 : RT.hop[cur][v];
      for (int i = (int)chain.size() - 1; i >= 0; --i)
        RT.hop[chain[i]][v] = base + ((int)chain.size() - i);
    }
  }
  RT.bnodes = M.strongholds;
  RT.bnodes.push_back(M.my_hq);
  RT.bnodes.push_back(M.opp_hq);
  std::sort(RT.bnodes.begin(), RT.bnodes.end());
  RT.bnodes.erase(std::unique(RT.bnodes.begin(), RT.bnodes.end()), RT.bnodes.end());
  RT.bidx.assign(N, -1);
  for (int i = 0; i < (int)RT.bnodes.size(); ++i) RT.bidx[RT.bnodes[i]] = i;
  return RT;
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
} // namespace presets

static Cmds decide(const SimState &S, const GameMap &M, const Paths &P, const RelayTab &RT,
                   int turn, BotState &bs, const Policy &pol) {
  Cmds a;
  const Side me = M.my_side, opp = opposite(me);
  const int my_hq = M.my_hq, opp_hq = M.opp_hq, N = M.N;
  long long budget = S.gold[(int)me];
  const Building *hq = building_at(S, my_hq);
  const Building *ohq = building_at(S, opp_hq);
  const int my_whp = HQ_LEVELS[hq ? hq->level : 1].warrior_hp;
  const int train_cap = HQ_LEVELS[hq ? hq->level : 1].train_cap;

  std::set<int> lab;
  std::vector<const Warrior *> mobile = partition_units(S, M, lab, bs.mission);
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
  // 기지 경유 홉 단축(사용자): 레퍼리는 가중 최단경로로 보내 홉(=턴)이 최소가 아닐 수 있다. 내 건물을
  // 무료 경유점으로 써 홉이 줄면 그리로 먼저 보낸다(도착 1턴+ 단축, 154090 B9: 직행5 vs 101경유4).
  // 최종 목적지를 route_to에 기록해 inflight 추적이 경유점(건물)에 속지 않게 한다. 경유점 도착 후엔
  // 다음 턴 재파견으로 이어간다. (도착=레퍼리 정확 홉수 turns_to로 비교.)
  auto issue_route = [&](const Warrior *w, int dest) -> bool {
    if (!w || used.count(w->id.num) || w->region == dest || !reachable(P, w->region, dest)) return false;
    long long best = turns_to(P, N, w->region, dest);
    int first = dest;
    for (const Building *b : my_b) {
      int W = b->region;
      if (W == w->region || W == dest || !reachable(P, w->region, W) || !reachable(P, W, dest)) continue;
      long long h = turns_to(P, N, w->region, W) + turns_to(P, N, W, dest);
      if (h < best) { best = h; first = W; }
    }
    if (!issue_move(w, first)) return false;
    bs.route_to[w->id.num] = dest; // 직행도 등록: 발령 턴(아직 정지 상태)에도 inflight/committed 추적 일관
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
  poi.push_back(opp_hq); // (centerRelapse #3) 귀환 이동이 wild로 남아 경합 오인되는 것 축소
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
  std::map<int, int> prev_opp = bs.prev_opp; // 직전 턴 상대 위치 스냅(아래 매스출격 감지용 — 곧 덮음)
  std::set<int> opp_moved; // 이번 턴 위치가 변한 상대(경합 확정 판별용, centerRelapse §0)
  for (auto &kv : cur_opp) {
    auto pit = prev_opp.find(kv.first);
    if (pit != prev_opp.end() && pit->second != kv.second) opp_moved.insert(kv.first);
  }
  bs.pred = npred;
  bs.prev_opp = cur_opp;

  // === [하드코딩 rushguard v2] 초반 5기 뭉침 감지 → 확장 취소·지연 소환 수비 =====
  // ① 감지 = 10턴 이내, 상대 HQ 포함 아무 구역에 상대 유닛 5기+ 뭉침(스테이징부터).
  if (!bs.rush_guard && turn <= 10) {
    std::map<int, int> clump;
    for (const auto &w : S.warriors)
      if (w.id.side == opp && w.hp > 0) clump[w.region]++;
    for (auto &kv : clump)
      if (kv.second >= 5) { bs.rush_guard = true; break; }
    if (bs.rush_guard) {
      // 회수 집결지 = 첫 번째로 지은 기지(이 시점 현존 기지는 사실상 1개; 여럿이면
      // HQ 최근접). 아직 없으면 HQ.
      bs.rush_rally = my_hq;
      long long bd = PINF;
      for (auto b : my_b)
        if (b->type == BType::BASE && reachable(P, my_hq, b->region) && P.dist[my_hq][b->region] < bd) {
          bd = P.dist[my_hq][b->region];
          bs.rush_rally = b->region;
        }
      bs.rush_engaged = false;
      bs.rush_intruders.clear(); // ④ 새 에피소드 = 새 명부
      // ② 두 번째 건물 스케줄링 취소: 진행 중이던 정착·중앙·공세 커밋 전부 파기
      bs.mission.clear();
      bs.route_to.clear();
      bs.jit_target = -1;
      bs.atk_target = -1;
      bs.atk_deadline = -1;
    }
  }
  if (bs.rush_guard) {
    // ④ 해제 = '들어왔던 유닛이 다 갈린 직후'(침입자 명부): 위협권(이번 턴 내 HQ쪽
    // 전진 or HQ 6턴권 or 우리 절반)에 든 상대를 명부에 등록, 자기 진영 회군 유닛은
    // 말소. 명부 생존자 0 & 상대 HQ 스테이징 4기 미만이어야 해제 → 잔당·트리클이
    // 오는 중 HQ 앞이 잠깐 비어도(웨이브 사이) 풀리지 않는다(이전 threat<3 구멍).
    // 스테이징 조건은 출격 전(아직 상대 절반, 명부 공백) 즉시 해제 구멍 봉인.
    int staged = 0, gate_ct = 0;
    for (const auto &w : S.warriors) {
      if (w.id.side != opp || w.hp <= 0) continue;
      if (w.region == opp_hq) staged++;
      if (w.region == my_hq ||
          std::binary_search(M.adj[my_hq].begin(), M.adj[my_hq].end(), w.region))
        gate_ct++;
      if (!reachable(P, w.region, my_hq)) continue;
      long long doo = reachable(P, w.region, opp_hq) ? P.dist[w.region][opp_hq] : 0;
      auto pit = prev_opp.find(w.id.num);
      bool advancing = pit != prev_opp.end() && pit->second != w.region &&
                       reachable(P, pit->second, my_hq) &&
                       P.dist[w.region][my_hq] < P.dist[pit->second][my_hq];
      bool retreating = pit != prev_opp.end() && pit->second != w.region &&
                        reachable(P, pit->second, my_hq) &&
                        P.dist[w.region][my_hq] > P.dist[pit->second][my_hq];
      if (advancing || turns_to(P, N, w.region, my_hq) <= 6 ||
          P.dist[w.region][my_hq] <= doo)
        bs.rush_intruders.insert(w.id.num); // 위협권 진입 = 명부 등록
      else if (w.region == opp_hq || retreating)
        bs.rush_intruders.erase(w.id.num); // '확실한 회군'(자기 HQ 복귀/멀어지는 이동)만
                                           // 말소 — 자기 절반에서의 중도 정지(스테이징
                                           // 일시정지)는 명부 유지(정지=해제 구멍 방지)
    }
    int roster_alive = 0; // 명부 생존자(죽은 유닛은 S.warriors에 없음)
    for (const auto &w : S.warriors)
      if (w.id.side == opp && w.hp > 0 && bs.rush_intruders.count(w.id.num)) roster_alive++;
    if (roster_alive == 0 && staged < 4) {
      bs.rush_guard = false; // 명부 전멸 → 이하 원래 확장기지최적화 스케줄링 재개
      bs.rush_intruders.clear();
      if (bs.rush_engaged) {
        bs.rush_done = true; // ⑥ 상비군 조기개방 재점화 금지
        // ⑦ 격퇴 원상복구: ③이 첫 기지 노동자를 HQ로 소환한 채 두면 기지 work_cap이
        // 빈다(-15/턴; 백필 캐스케이드는 설계 판정 ②로 삭제돼 돌려보낼 주체가 없음).
        // 해제 턴에 HQ 잉여(저HP=노동 우선, HQ 노동 몫은 잔류)를 부족분만큼 복귀.
        int rally = bs.rush_rally;
        const Building *rb = rally >= 0 ? building_at(S, rally) : nullptr;
        if (rb && rb->side == me && rb->type == BType::BASE) {
          int have = 0;
          for (const auto &w : S.warriors)
            if (w.id.side == me && w.hp > 0 && w.region == rally) have++;
          int need = rb->work_cap() - have;
          std::vector<const Warrior *> at_hq;
          for (const auto &w : S.warriors)
            if (w.id.side == me && w.hp > 0 && w.state == WState::STATIONARY &&
                w.region == my_hq && !used.count(w.id.num))
              at_hq.push_back(&w);
          std::sort(at_hq.begin(), at_hq.end(),
                    [](const Warrior *x, const Warrior *y) { return x->hp < y->hp; });
          // 기지 우선(생존 1기여도 기지로): HQ 노동 슬롯은 이후 신병 스폰이 자연
          // 백필하지만 기지는 채울 주체가 없다. 소수 생존 시 HQ가 비는 건 감수.
          for (int i = 0; i < (int)at_hq.size() && need > 0; ++i)
            if (issue_move(at_hq[i], rally)) need--;
        }
      }
      bs.rush_engaged = false;
    } else {
      bs.center_war = false; // 러시 경로가 중앙을 지나도 경합전쟁 스티키 오발 방지
      // 집결지 = 현존 기지 중 HQ 최근접, 매턴 실시간 재계산. 감지 시점 캐시를 쓰면
      // 감지와 첫 기지 UPGRADE가 같은 턴일 때 캐시=HQ로 굳어 ⑤가 '기지 없음'으로
      // 오판, 다음 빈 거점에 두 번째 기지를 지어버림(bench s103 T9 실측 — 사용자
      // 관측 '중앙 유닛이 이상한 데 가서 건설'의 근본 원인).
      int rally = -1;
      {
        long long brd = PINF;
        for (auto b : my_b)
          if (b->type == BType::BASE && reachable(P, my_hq, b->region) &&
              P.dist[my_hq][b->region] < brd) {
            brd = P.dist[my_hq][b->region];
            rally = b->region;
          }
      }
      // ⑤ 첫 기지는 무조건(매스 감지와 무관): 기지가 정말 없으면(미건설/소실) 집결지 =
      // 첫 기지 자리(우리쪽 최근접 빈 거점), 정지 유닛이 그 위에 서면 즉시 건설.
      // (UPGRADE는 적 유닛 있는 구역 발령 = WA 몰수라 사전 차단.)
      if (rally < 0) {
        rally = my_hq;
        int tsh = -1;
        long long bd2 = PINF;
        for (int sh : M.strongholds) {
          if (building_at(S, sh) || !reachable(P, my_hq, sh) || !on_my_side(sh)) continue;
          if (P.dist[my_hq][sh] < bd2) { bd2 = P.dist[my_hq][sh]; tsh = sh; }
        }
        if (tsh >= 0) {
          rally = tsh;
          if (!enemy_warrior_at(S, tsh, me) && budget >= BASE_BUILD_COST)
            for (const auto &w : S.warriors)
              if (w.id.side == me && w.hp > 0 && w.state == WState::STATIONARY &&
                  w.region == tsh && !used.count(w.id.num)) {
                a.upgrades.push_back(tsh);
                budget -= BASE_BUILD_COST;
                used.insert(w.id.num); // 건설자는 제자리 고정
                break;
              }
        }
      }
      bs.rush_rally = rally; // ⑦(해제 복구)용 캐시 — 매턴 실시간 값으로 갱신
      // ③ 트리거 = 상대 매스가 내 HQ 한 칸 앞(인접 구역) 또는 HQ 위 도달(스티키).
      if (gate_ct > 0) bs.rush_engaged = true;
      if (bs.rush_engaged) {
        // 그 시점부터 매턴 계속 훈련(골드/생산캡 한도) — 스폰이 이동 후·전투 전이라
        // 같은 턴 전투부터 합류. 공성당하는 동안도 생산 지속.
        if (hq && budget >= TRAIN_COST) {
          int n = (int)std::min<long long>((long long)train_cap, budget / TRAIN_COST);
          if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; }
        }
        // 첫 기지에 있던 유닛(최대 2기) HQ 소환 + 떠돌이는 HQ 합류.
        int pulled = 0;
        for (const auto &w : S.warriors) {
          if (w.id.side != me || w.hp <= 0 || w.state != WState::STATIONARY) continue;
          if (used.count(w.id.num) || w.region == my_hq) continue;
          if (w.region == rally && rally != my_hq) {
            if (pulled < 2 && issue_move(&w, my_hq)) pulled++;
            continue;
          }
          const Building *wb = building_at(S, w.region);
          if (wb && wb->side == me) continue; // 다른 내 건물 위 = 근무 유지
          issue_move(&w, my_hq);
        }
      } else {
        // ② '해당 유닛만' 회수: 내 건물 밖 떠돌이(정착민)만 첫 기지로. 노동자는 제자리
        // 근무(수입 유지). 이동 중 유닛은 재명령 불가 → 도착 후 다음 턴 회수.
        for (const auto &w : S.warriors) {
          if (w.id.side != me || w.hp <= 0 || w.state != WState::STATIONARY) continue;
          if (used.count(w.id.num)) continue;
          const Building *wb = building_at(S, w.region);
          if (wb && wb->side == me) continue; // 내 건물 위 = 근무 유지
          issue_move(&w, rally);
        }
      }
      return a; // 그 외 스케줄링(확장 건설·업글·잉여 훈련·공세) 전면 동결 = 골드 뱅킹
    }
  }

  // === (centerRelapse) 고정 중앙 거점 + 경합 '확정' 스티키 =======================
  // true_center = 우리쪽/중앙(doo>=dm) 요새 중 |doo−dm| 최소(등거리 기하중심).
  int true_center = -1;
  { long long bc = PINF, bt = PINF;
    for (int sh : M.strongholds) {
      if (!reachable(P, my_hq, sh)) continue;
      long long dm = P.dist[my_hq][sh];
      long long doo = reachable(P, opp_hq, sh) ? P.dist[opp_hq][sh] : PINF;
      if (doo < dm) continue;
      long long key = (doo < PINF) ? (doo - dm) : dm;
      if (key < bc || (key == bc && dm < bt)) { bc = key; bt = dm; true_center = sh; }
    }
  }
  // 경합 '확정' = 상대 이동 관측 + pred 남은 모든 후보로 가는 레퍼리 경로가 중앙을
  // 통과(향후 보행이 중앙을 반드시 밟음), 또는 중앙 위 정지. pred '포함'만으론 초기
  // 광역 팬텀(아무도 중앙에 안 가는데 발동). 한 번 확정되면 중앙 완공까지 유지(스티키
  // — 공멸 직후 관측 공백에 판정이 꺼지면 스케줄러가 중앙에서 눈을 뗌). 인접 정지
  // (기지 노동자 오발원)·wild 제외.
  {
    const Building *cb = true_center >= 0 ? building_at(S, true_center) : nullptr;
    // 중앙 경합 비가역성(사용자 07-07): 누구든 중앙에 한 번이라도 지었으면 초반 경합
    // 국면은 영구 종료 — 중후반에 그 건물이 파괴돼 중앙이 다시 빈 거점이 되고 상대
    // 웨이브가 경유해도 war 재점화 금지(재점화 시 판정 ③⑨의 올인 스택·경제 동결이
    // 중후반에 잘못 재적용돼 상비군이 중앙으로 빨려 들어가는 블랙홀). 재건 자체는
    // 정상 창 판정(!center_charged && ctr_in_win) 경로로 계속 가능.
    if (cb != nullptr) bs.center_ever_built = true;
    if (true_center < 0 || cb != nullptr || bs.center_ever_built) {
      bs.center_war = false; // 해결(내/상대 완공 or 이력) = 전쟁 종료(경계와 동형)
    } else {
      bool obs = false;
      for (const auto &w2 : S.warriors) {
        if (w2.id.side == me || w2.hp <= 0) continue;
        if (w2.region == true_center) { obs = true; break; }
        if (!opp_moved.count(w2.id.num)) continue;
        auto pit = bs.pred.find(w2.id.num);
        if (pit == bs.pred.end() || pit->second.empty()) continue; // wild = 미확정
        bool all_via = true;
        for (int c : pit->second) {
          bool via = false;
          int u = w2.region, g = 0;
          for (; g <= N + 1; ++g) {
            if (u == true_center) { via = true; break; }
            if (u == c) break;
            u = P.nxt[u][c];
            if (u < 0) break;
          }
          if (!via) { all_via = false; break; }
        }
        if (all_via) { obs = true; break; }
      }
      if (obs) bs.center_war = true;
    }
  }
  const bool war = bs.center_war; // 경합 전쟁: 경제를 전쟁에만(판정 ⑨⑩), 스택 무제한
#ifdef CRLDBG
  if (g_crl_live)
    std::cerr << "# CRL T" << turn << " ctr=" << true_center << " war=" << (war ? 1 : 0)
              << " gold=" << S.gold[(int)me] << "\n";
#endif

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
      hq_threatened = true;
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
  // 수상한 움직임 = 상대 '매스 출격' 감지(사용자 구체화). 정적 집결 카운트가 아니라 상대 유닛의 직전
  // 위치(prev_opp)를 추적해 '움직임 개시/전진'을 직접 본다: ① 한 소스 거점에서 3기+가 이번 턴 우리쪽으로
  // 이동 개시(확장 매스 or 공격 매스 — 정착민 다수 동시출발/기지에 묶여있던 잉여 일제 출격), ② 우리쪽으로
  // 전진 중인 상대 잉여가 3기+ & 내 야전 이상 누적. sortie_from = 소스 거점별 우리쪽 출격 수(= 요청한
  // '거점별 잉여 카운팅'의 실효 형태 — 어느 거점서 몇 기가 우리 쪽으로 떴나). opp_field = 야전(기동) 잉여.
  int opp_field = 0;
  int my_field = (int)mobile.size();
  for (const auto &w : S.warriors) {
    if (w.id.side != opp || w.hp <= 0) continue;
    const Building *wb = building_at(S, w.region);
    if (!(wb && wb->side == opp)) opp_field++; // 자기 건물 밖 = 야전(기동) 잉여
  }
  std::map<int, int> sortie_from; // 소스(직전 위치) → 이번 턴 우리쪽으로 전진한 상대 유닛 수
  int advancing_total = 0;        // 우리쪽으로 전진 중(전진 이동 or 이미 우리 절반)인 상대 잉여 총량
  for (const auto &w : S.warriors) {
    if (w.id.side != opp || w.hp <= 0 || !reachable(P, w.region, my_hq)) continue;
    auto pit = prev_opp.find(w.id.num);
    bool toward_us = pit != prev_opp.end() && pit->second != w.region &&
                     reachable(P, pit->second, my_hq) &&
                     P.dist[w.region][my_hq] < P.dist[pit->second][my_hq]; // 이번 턴 내 HQ에 더 가까워짐
    bool on_my_half = reachable(P, w.region, opp_hq) &&
                      P.dist[w.region][my_hq] < P.dist[w.region][opp_hq];  // 이미 중앙선 넘어 우리 절반
    if (toward_us) sortie_from[pit->second]++;
    if (toward_us || on_my_half) advancing_total++;
  }
  int max_sortie = 0;
  for (auto &kv : sortie_from) max_sortie = std::max(max_sortie, kv.second);
  // brace = ① 한 소스서 3기+ 매스 출격, 또는 ② 우리쪽 전진 잉여 3기+ & 내 야전 이상(선제 대응 필요).
  bool opp_bracing = max_sortie >= 3 || (advancing_total >= 3 && advancing_total >= my_field + 1);
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
  // [사용자] HQ업 밸류 완화: 이미 앞서 있을 때(경제 압도 or HQ 2렙+ 리드) 재량 업글을
  // 보류하고 잉여를 병력에 먼저 투자. 단 상대 이상 유지(패리티·추격) 트리거는 불가침 —
  // 여기서 손대면 래더 판정을 헌납. income_dom(1.54x/600)보다 훨씬 센 문턱만 발동.
  int my_hq_lvl = hq ? hq->level : 0;
  bool econ_crush = (long long)side_income(S, me) >= (long long)side_income(S, opp) * 2 &&
                    (S.gold[(int)me] - S.gold[(int)opp]) > 1500;
  bool hq_lead2 = opp_hq_lvl >= 1 && my_hq_lvl - opp_hq_lvl >= 2;
  bool hold_hq = my_hq_lvl >= 2 && (econ_crush || hq_lead2);
  bool did_hq = false;
  // (판정 ⑩) 경합 전쟁 중 경제는 전쟁 전용 — HQ 업글(패리티 추격 포함) 동결.
  if (!war && hq && !hq_threatened && garrison_at(my_hq) >= 1 && !enemy_warrior_at(S, my_hq, me) &&
      hq->level < HQ_MAX_LEVEL) {
    // HQ 레벨을 상대 이상으로: train_cap(생산)·work_cap(경제)·turret+maxHP(수비·판정).
    // opp_hq_lvl>=hq->level(패리티/추격)은 hold_hq와 무관하게 항상 허용; 나머지 재량
    // 트리거(선제 렙업·경제형·late HP뱅크)만 hold_hq로 억제.
    bool want = ((opp_hq_lvl >= hq->level) ||
                 ((hq->level == 1 || turn >= pol.late_game || (pol.deep_hq && hq->level < 4) ||
                   (income_dom && hq->level < 3)) && !hold_hq)) &&
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
  if (!war && hq && hq->level < HQ_MAX_LEVEL && (!deep_behind || ladder_window) && !did_hq && army_ok &&
      (opp_hq_lvl >= hq->level ||
       (!hold_hq && ((income_dom && hq->level < 3) || turn >= pol.late_game))))
    hq_fund = HQ_LEVELS[hq->level + 1].upgrade_cost;
  // 확정 경합 레이서 ETA: sh를 실제로 노리는 상대만(유령 배제). strict=true = "확정"만
  // (sh 위/인접 정지 유닛, 또는 narrowed 예측이 sh를 콕 집은 MOVING 유닛). strict=false는
  // 기존 dispatch 판정과 동일(예측 없는 MOVING도 레이서로 봄).
  auto enemy_race_eta = [&](int sh, bool strict) -> long long {
    long long eta = PINF;
    for (const auto &w2 : S.warriors) {
      if (w2.id.side == me || w2.hp <= 0) continue;
      bool racer = false;
      if (w2.state == WState::MOVING) {
        auto pit2 = bs.pred.find(w2.id.num);
        bool wild = (pit2 == bs.pred.end()) || pit2->second.empty();
        bool specific = !wild && pit2->second.count(sh);
        racer = specific || (wild && !strict);
      } else {
        racer = (w2.region == sh);
        if (!racer)
          for (int nb : M.adj[sh]) if (w2.region == nb) { racer = true; break; }
      }
      if (!racer) continue;
      int loc2 = (w2.state == WState::MOVING) ? w2.target : w2.region;
      if (reachable(P, loc2, sh)) eta = std::min(eta, turns_to(P, N, loc2, sh)); // 레퍼리 정확 턴수(홉)
    }
    return eta;
  };
  // ============ centerRelapse 이식: 정착·확장 파이프라인 (판정 ①~⑩) ============
  // 이동층 = #2 무빙테이블 + relay_dispatch 단일 출구. 기존 route_to 정착 추적·
  // 노동자 백필 캐스케이드·1턴 이득 게이트·force_center 하드코딩은 제거(route_to는
  // 공세 웨이브 경유 추적 전용으로 존속). 정착 최종지의 단일 진실 = bs.mission.
  auto hops = [&](int u, int v) -> long long {
    int h = RT.hop[u][v];
    return h < 0 ? PINF : (long long)h;
  };
  auto my_bldg = [&](int r) -> const Building * {
    const Building *b = building_at(S, r);
    return (b && b->side == me) ? b : nullptr;
  };
  auto my_warrior_by_num = [&](int num) -> const Warrior * {
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.id.num == num) return &w;
    return nullptr;
  };
  auto manned = [&](int r) -> bool { // 릴레이 보디 존재(유인 판정)
    for (const auto &w : S.warriors)
      if (w.id.side == me && w.hp > 0 && w.region == r && w.state == WState::STATIONARY &&
          !bs.mission.count(w.id.num) && !used.count(w.id.num))
        return true;
    return false;
  };
  // 무빙테이블 DP(#2b): mvt[s][d] = min(직행, h(s,m)+mvt[m][d]−유인크레딧). 전진
  // 경유만(#5 3차 수정: 목적지 홉수 엄격 감소 — equal-net 후진/수평 경유는 크레딧
  // 불발 시 +1턴 처닝·바운스의 원천이라 금지).
  const int NB = (int)RT.bnodes.size();
  std::vector<char> ownedB(NB, 0), mannedB(NB, 0);
  for (int i = 0; i < NB; ++i) {
    ownedB[i] = my_bldg(RT.bnodes[i]) ? 1 : 0;
    mannedB[i] = (ownedB[i] && manned(RT.bnodes[i])) ? 1 : 0;
  }
  auto run_mvt = [&](const std::vector<char> &blocked) {
    std::vector<std::vector<long long>> t(NB, std::vector<long long>(NB));
    for (int si = 0; si < NB; ++si)
      for (int di = 0; di < NB; ++di)
        t[si][di] = RT.hop[RT.bnodes[si]][RT.bnodes[di]];
    for (bool chg = true; chg;) {
      chg = false;
      for (int si = 0; si < NB; ++si)
        for (int di = 0; di < NB; ++di) {
          if (si == di || t[si][di] < 0) continue;
          for (int mi = 0; mi < NB; ++mi) {
            if (mi == si || mi == di || !ownedB[mi] || blocked[mi] || t[mi][di] < 0) continue;
            int h1 = RT.hop[RT.bnodes[si]][RT.bnodes[mi]];
            if (h1 < 1) continue;
            int hm = RT.hop[RT.bnodes[mi]][RT.bnodes[di]];
            int hs = RT.hop[RT.bnodes[si]][RT.bnodes[di]];
            if (hm < 0 || (hs >= 0 && hm >= hs)) continue; // 전진 경유만
            long long val = h1 + t[mi][di] - (mannedB[mi] ? 1 : 0);
            if (val < t[si][di]) { t[si][di] = val; chg = true; }
          }
        }
    }
    return t;
  };
  std::vector<std::vector<long long>> mvt = run_mvt(std::vector<char>(NB, 0));
  auto eta_sched = [&](int s, int d) -> long long { // 내 이동 계획 ETA(상대 예측엔 금지)
    if (s == d) return 0;
    int si = RT.bidx[s], di = RT.bidx[d];
    if (si >= 0 && di >= 0 && mvt[si][di] >= 0) return mvt[si][di];
    return hops(s, d);
  };
  auto pick_relay_body = [&](int W) -> const Warrior * {
    std::vector<const Warrior *> cand;
    for (const auto &l : S.warriors)
      if (l.id.side == me && l.hp > 0 && l.region == W && l.state == WState::STATIONARY &&
          !bs.mission.count(l.id.num) && !used.count(l.id.num))
        cand.push_back(&l);
    if (cand.empty()) return nullptr;
    if (W == my_hq && bs.jit_target >= 0 && (int)cand.size() < 2) return nullptr; // JIT 커밋 예약
    std::sort(cand.begin(), cand.end(), [&](const Warrior *x, const Warrior *y) {
      bool xw = lab.count(x->id.num) > 0, yw = lab.count(y->id.num) > 0;
      if (xw != yw) return !xw;       // 비노동 잉여 우선(노동 무교란)
      if (x->hp != y->hp) return x->hp > y->hp;
      return x->id.num < y->id.num;
    });
    return cand.front();
  };
  // relay_dispatch = 정착·확장 이동의 단일 출구(centerRelapse §1.5). 첫 레그 = net
  // argmin(전진 경유만), '1홉 유인 링크'는 같은아침 스왑 루프(-1턴 회수). 반환 =
  // 계획 net ETA, <0 = 발령 실패(발령 전 상태 무변).
  auto relay_dispatch = [&](const Warrior *w0, int dest) -> long long {
    if (!w0 || w0->state != WState::STATIONARY || w0->region == dest) return -1;
    if (used.count(w0->id.num) || hops(w0->region, dest) >= PINF) return -1;
    long long promise = -1;
    BotState::Mission ms;
    auto mit0 = bs.mission.find(w0->id.num);
    if (mit0 != bs.mission.end() && mit0->second.dest == dest) ms = mit0->second;
    else {
      ms.dest = dest; ms.origin = w0->region; ms.start_turn = turn;
      ms.direct_h = hops(w0->region, dest);
    }
    const Warrior *w = w0;
    bool issued = false;
    for (;;) {
      int s0 = w->region;
      long long direct = hops(s0, dest);
      long long bnet = direct, brem = -1;
      int bkind = 2, bfirst = dest;
      int si = RT.bidx[s0], di = RT.bidx[dest];
      if (si >= 0 && di >= 0) {
        std::vector<char> blocked(NB, 0); // visited∪현위치 마스킹(바운스 차단)
        blocked[si] = 1;
        for (int vb : ms.visited) if (RT.bidx[vb] >= 0) blocked[RT.bidx[vb]] = 1;
        std::vector<std::vector<long long>> mv2 = run_mvt(blocked);
        for (int mi = 0; mi < NB; ++mi) {
          if (mi == si || mi == di || !ownedB[mi] || blocked[mi] || mv2[mi][di] < 0) continue;
          int m = RT.bnodes[mi];
          if (enemy_warrior_at(S, m, me)) continue; // 적 동거 경유지 = 핀 리스크
          long long h1 = RT.hop[s0][m];
          if (h1 < 1) continue;
          long long hm2 = hops(m, dest);
          if (hm2 >= PINF || hm2 >= direct) continue; // 전진 경유만
          int cr = mannedB[mi] ? 1 : 0;
          long long rem = mv2[mi][di];
          long long val = h1 + rem - cr;
          if (val > direct) continue; // net 초과 제외(동률은 전진 경유 채택)
          int kind = cr ? 0 : 1;
          bool better = false;
          if (val != bnet) better = val < bnet;
          else if (kind != bkind) better = kind < bkind;
          else if (brem >= 0 && rem != brem) better = rem < brem;
          if (better) { bnet = val; bkind = kind; bfirst = m; brem = rem; }
        }
      }
      if (promise < 0) promise = bnet;
      if (ms.promise < 0) ms.promise = bnet; // 최초 발령 계획 보존(이관 시 carry)
      int fee = my_bldg(bfirst) ? 0 : MOVE_COST;
      if (budget < fee) {
        if (issued) bs.mission[w->id.num] = ms; // 골드 소진 — 다음 턴 continuation
        break;
      }
      budget -= fee;
      a.moves.push_back({w->id.num, bfirst});
      used.insert(w->id.num);
      issued = true;
#ifdef CRLDBG
      if (g_crl_live) {
        std::cerr << "# CRL T" << turn << " RDISP " << w->id.num << " " << s0 << "->" << dest
                  << " direct=" << direct;
        if (bfirst != dest)
          std::cerr << " via=" << bfirst << " net=" << bnet << " manned=" << (bkind == 0 ? 1 : 0);
        std::cerr << " fee=" << fee << "\n";
      }
#endif
      if (bfirst == dest) { bs.mission[w->id.num] = ms; break; }
      ms.visited.push_back(bfirst);
      const Warrior *L = nullptr;
      if (hops(s0, bfirst) == 1 && !enemy_warrior_at(S, bfirst, me) && !enemy_warrior_at(S, s0, me))
        L = pick_relay_body(bfirst); // 같은윈도 스왑(1홉 유인 링크 -1 회수)
      if (!L) { bs.mission[w->id.num] = ms; break; }
      bs.mission.erase(w->id.num); // w는 bfirst 착지·노동 백필
      lab.erase(L->id.num);
      ms.fires++;
#ifdef CRLDBG
      if (g_crl_live)
        std::cerr << "# CRL T" << turn << " FIRE0 W=" << bfirst << " in=" << w->id.num
                  << " L=" << L->id.num << " dest=" << dest << " (-1 same-window)\n";
#endif
      w = L;
    }
    return issued ? promise : -1;
  };

  // (a) 미션 클린업 — 사망/도착/차출(used=수비 draft·건설 소진, 판정 ⑥ 회수 허용)/
  //     표적 무효(중앙만 적 점유 면제) + 비중앙 release(확정 레이스 열세 시 포기 →
  //     최근접 재확장 — goldenhammer 검증 규칙 유지, 중앙은 하드커밋 면제).
  std::set<int> released;
  for (auto it = bs.mission.begin(); it != bs.mission.end();) {
    const Warrior *w = my_warrior_by_num(it->first);
    bool drop = false;
    if (!w || w->hp <= 0) drop = true;
    else if (w->state == WState::STATIONARY && w->region == it->second.dest) {
      drop = true; // 도착
#ifdef CRLDBG
      if (g_crl_live)
        std::cerr << "# CRL T" << turn << " DONE " << it->first << " dest=" << it->second.dest
                  << " elapsed=" << (turn - it->second.start_turn)
                  << " promise=" << it->second.promise
                  << " direct=" << it->second.direct_h << " fires=" << it->second.fires
                  << " origin=" << it->second.origin << "\n";
#endif
    } else if (used.count(it->first)) drop = true; // 수비/건설이 소진
    else {
      int d = it->second.dest;
      if (!is_sh(d) || building_at(S, d) || (enemy_warrior_at(S, d, me) && d != true_center))
        drop = true;
      else if (d != true_center) {
        long long rem = (w->state == WState::MOVING)
                            ? hops(w->region, w->target) + eta_sched(w->target, d)
                            : eta_sched(w->region, d);
        long long ee = enemy_race_eta(d, /*strict=*/true);
        if (ee < PINF && rem < PINF && ee <= rem) { drop = true; released.insert(it->first); }
      }
    }
    if (drop) it = bs.mission.erase(it);
    else ++it;
  }
  // (b) 릴레이 발사 스캔: 인바운드 잔여 1홉(다음 아침 착지 확정) → 경유지 보디를
  //     다음 레그로 지금 출발(-1턴, 착지 백필 = 수입 공백 0).
  for (const auto &w : S.warriors) {
    if (w.id.side != me || w.hp <= 0 || w.state != WState::MOVING) continue;
    auto mit = bs.mission.find(w.id.num);
    if (mit == bs.mission.end()) continue;
    int W = w.target;
    if (W == mit->second.dest || !my_bldg(W)) continue;
    if (hops(w.region, W) != 1) continue;
    if (enemy_warrior_at(S, W, me) || enemy_warrior_at(S, w.region, me)) continue;
    const Warrior *L = pick_relay_body(W);
    if (!L) continue;
    BotState::Mission carry = mit->second;
    carry.visited.push_back(W);
    carry.fires++;
    bs.mission[L->id.num] = carry;
    if (relay_dispatch(L, carry.dest) < 0) { bs.mission.erase(L->id.num); continue; }
    bs.mission.erase(w.id.num);
    lab.erase(L->id.num);
#ifdef CRLDBG
    if (g_crl_live)
      std::cerr << "# CRL T" << turn << " FIRE W=" << W << " in=" << w.id.num << "@" << w.region
                << " L=" << L->id.num << " dest=" << carry.dest << " (-1)\n";
#endif
  }
  // (c) continuation: 경유지 도착 캐리어의 다음 레그 재발령(정지 손실 0).
  for (const auto &w : S.warriors) {
    if (w.id.side != me || w.hp <= 0 || w.state != WState::STATIONARY) continue;
    auto mit = bs.mission.find(w.id.num);
    if (mit == bs.mission.end() || w.region == mit->second.dest) continue;
    if (used.count(w.id.num) || enemy_warrior_at(S, w.region, me)) continue;
    relay_dispatch(&w, mit->second.dest);
  }

  // §3 커밋/parr/doomed. (판정 ⑧) 경합 확정 중 중앙행 커밋 = 건설 불성립 가정 —
  // 300 예약(parr)과 밥값(doomed)을 중앙 스케줄링 투영에서만 제외.
  std::vector<long long> parr;
  std::set<int> committed_sh;
  int doomed = 0, committed_total = 0, center_cnt = 0;
  for (const auto &pr : bs.mission) { // Pass A: 미션이 커밋의 단일 진실
    const Warrior *w = my_warrior_by_num(pr.first);
    if (!w) continue;
    int d = pr.second.dest;
    if (built_here.count(d)) continue;
    committed_sh.insert(d);
    committed_total++;
    if (d == true_center) center_cnt++;
    long long rem = (w->state == WState::MOVING)
                        ? hops(w->region, w->target) + eta_sched(w->target, d)
                        : eta_sched(w->region, d);
    if (rem >= PINF) rem = hops(w->region, d) < PINF ? hops(w->region, d) : 0;
    if (!(d == true_center && war)) parr.push_back(std::max(0LL, rem));
    else doomed++;
    if (w->state == WState::STATIONARY) used.insert(w->id.num); // 경유 대기 캐리어 잠금
  }
  for (const auto &w : S.warriors) { // Pass B: 미션 없는 도착·대기 정착민(빈 요새 위)
    if (w.id.side != me || w.hp <= 0) continue;
    if (used.count(w.id.num) || bs.mission.count(w.id.num) || lab.count(w.id.num)) continue;
    int loc = (w.state == WState::MOVING) ? w.target : w.region;
    if (!is_sh(loc) || building_at(S, loc) || built_here.count(loc)) continue;
    if (enemy_warrior_at(S, loc, me) && loc != true_center) continue; // 중앙만 대치 집계
    committed_sh.insert(loc);
    committed_total++;
    if (loc == true_center) center_cnt++;
    long long rem = (w.state == WState::MOVING && hops(w.region, loc) < PINF) ? hops(w.region, loc) : 0;
    if (!(loc == true_center && war)) parr.push_back(std::max(0LL, rem));
    else doomed++;
    if (w.state == WState::STATIONARY) used.insert(w.id.num); // 대기 정착민 고정
  }
  std::sort(parr.begin(), parr.end());
  int inflight = committed_total; // 하류 호환(oc 포화·rally expand_done)
  int inflight_nc = committed_total - center_cnt;

  // §4 후보 R + gold_ready(parr 큐 시뮬 — 기존 단식 gold_eta 대체, doomed 밥값 제외)
  struct CandR { int r; long long h; };
  std::vector<CandR> R;
  for (int sh : M.strongholds) {
    if (sh == true_center) continue; // 중앙 = 상시 슬롯 전담
    if (building_at(S, sh) || enemy_warrior_at(S, sh, me)) continue;
    if (built_here.count(sh) || committed_sh.count(sh)) continue;
    // (homeHalf 07-07) 상대쪽(doo < α·dm) 거점 제외 — 지어도 상대 증원 거리가
    // 짧아 금방 뺏긴다(순손실). 척도는 true_center 선정과 동일(맵 고정 기하속성
    // = 가중거리 P.dist, 상대 도달불가는 우리쪽 취급). α=0.90 = 중앙선 거의
    // 등거리까지만 개방(homeHalf α 이력 1.0→0.75→0.45→0.90). 정수 백분율 판정.
    {
      long long dm = P.dist[my_hq][sh];
      long long doo = reachable(P, opp_hq, sh) ? P.dist[opp_hq][sh] : PINF;
      if (doo < PINF && doo * 100 < 90 * dm) continue;
    }
    long long h = eta_sched(my_hq, sh);
    if (h >= PINF) continue;
    R.push_back({sh, h});
  }
  long long income_now = (long long)side_income(S, me) +
                         (long long)WORK_INCOME * (long long)built_here.size();
  auto gold_ready = [&](bool payTrain) -> long long {
    long long g = budget - (payTrain ? (long long)(TRAIN_COST + MOVE_COST) : 0);
    long long inc = income_now;
    long long uc = (long long)std::max(0, army_ct - doomed) + (payTrain ? 1 : 0);
    size_t done = 0;
    for (long long t2 = 0; t2 <= MAX_TURN; ++t2) {
      if (t2 > 0) g += inc - (long long)UPKEEP_PER_WARRIOR * uc;
      while (done < parr.size() && parr[done] <= t2 && g >= (long long)BASE_BUILD_COST) {
        g -= BASE_BUILD_COST;
        inc += WORK_INCOME;
        ++done;
      }
      if (done >= parr.size() && g >= (long long)BASE_BUILD_COST) return t2;
    }
    return MAX_TURN + 1;
  };
  auto pick_target = [&](long long horizon, const std::set<int> &tk) -> int {
    int best = -1; long long bh = 0; bool bin = false;
    for (const CandR &c : R) { // farthest-in-window(창 안=최전방, 창 밖=최근접)
      if (tk.count(c.r)) continue;
      bool in = c.h <= horizon;
      bool better;
      if (best < 0) better = true;
      else if (in != bin) better = in;
      else if (in) better = c.h > bh || (c.h == bh && c.r < best);
      else better = c.h < bh || (c.h == bh && c.r < best);
      if (better) { best = c.r; bh = c.h; bin = in; }
    }
    return best;
  };
  int nbase = (int)my_b.size() - 1;
  std::set<int> taken;
  int newly = 0;
  auto slot_active = [&]() -> bool { // 중앙 상시 슬롯(첫 기지 커밋 이후 ~ 완공까지)
    return true_center >= 0 && building_at(S, true_center) == nullptr &&
           !built_here.count(true_center) &&
           nbase + (int)built_here.size() + committed_total + newly >= 1;
  };
  bool center_charged = committed_sh.count(true_center) > 0;
  auto dispatch_to = [&](int tgt) -> bool { // 최근접 잉여 몸 파견(relay_dispatch 출구)
    const Warrior *body = nullptr;
    long long best = PINF;
    for (const auto &w : S.warriors) {
      if (w.id.side != me || w.hp <= 0 || w.state != WState::STATIONARY) continue;
      if (lab.count(w.id.num) || bs.mission.count(w.id.num) || used.count(w.id.num)) continue;
      if (released.count(w.id.num)) continue; // 포기 정착민 재선발 금지
      if (enemy_warrior_at(S, w.region, me)) continue;
      long long h = eta_sched(w.region, tgt);
      if (h < best) { best = h; body = &w; }
    }
    if (!body || budget < MOVE_COST) return false;
    long long r = relay_dispatch(body, tgt);
    if (r < 0) return false;
    if (!(tgt == true_center && war)) { parr.push_back(r); std::sort(parr.begin(), parr.end()); }
    else doomed++;
    taken.insert(tgt);
    committed_sh.insert(tgt);
    inflight++;
    newly++;
    if (tgt != true_center) inflight_nc++;
    return true;
  };

  // §5 파견: 지난 턴 JIT 커밋 이행 → 슬롯 루프(전쟁 = 중앙 무제한 스택 / 무경합 =
  // 창 판정 — 중앙 도착 > 300 준비면 지각 → 이 슬롯은 farthest, 중앙은 다음 슬롯).
  if (bs.jit_target >= 0) {
    int jt = bs.jit_target;
    bs.jit_target = -1;
    if (jt == true_center) {
      if ((!center_charged || war) && building_at(S, true_center) == nullptr)
        if (dispatch_to(jt)) center_charged = true;
    } else if (!committed_sh.count(jt)) {
      bool inR = false;
      for (const CandR &c : R) if (c.r == jt) { inR = true; break; }
      if (inR) dispatch_to(jt);
    }
  }
  // 직렬화 완화(비중앙 캡, 판정 ③: 중앙은 면제) + 러쉬 중 비중앙 확장 정지.
  int max_inflight = (!rush && budget >= 2 * BASE_BUILD_COST && my_b.size() >= 4) ? 2 : 1;
  bool rusher_hold = rush && (int)my_b.size() >= 2;
  for (;;) {
    long long horizon = gold_ready(false);
    long long ctr_eta_hq = true_center >= 0 ? eta_sched(my_hq, true_center) : PINF;
    bool ctr_in_win = ctr_eta_hq < PINF && ctr_eta_hq <= horizon;
    bool need_center = slot_active() && (war || (!center_charged && ctr_in_win));
    bool can_nc = !war && !rusher_hold && inflight_nc < max_inflight; // 전쟁 중 확장 정지(판정 ⑨)
    if (!need_center && !can_nc) break;
    int tgt = need_center ? true_center : pick_target(horizon, taken);
    if (tgt < 0) break;
    if (!dispatch_to(tgt)) break; // 잉여 소진 → §6 훈련으로
    if (need_center) center_charged = true;
  }
  // §6 JIT 훈련 + #4 HQ 스와프. 우선순위(판정 ⑤): 수비 비상(train_n 선점)이 이미
  // 앞에서 소비했으면 자연 양보, 상비군은 뒤(전쟁 중엔 동결). 스와프 게이트(판정 ⑦)
  // = 적 HQ 동거만 금지(centerRelapse 기준 — hq_threatened 게이트 없음).
  {
    bool need_center_train = slot_active() && (war || !center_charged);
    if (a.train_n == 0 && hq && budget >= TRAIN_COST &&
        (need_center_train || (!war && !rusher_hold && inflight_nc < max_inflight))) {
      const Warrior *swap_body = nullptr;
      if (!enemy_warrior_at(S, my_hq, me)) {
        for (const auto &w : S.warriors) {
          if (w.id.side != me || w.hp <= 0 || w.region != my_hq ||
              w.state != WState::STATIONARY || bs.mission.count(w.id.num) ||
              used.count(w.id.num) || released.count(w.id.num))
            continue;
          if (!swap_body || w.hp > swap_body->hp ||
              (w.hp == swap_body->hp && w.id.num < swap_body->id.num))
            swap_body = &w; // 최고HP 우선(중앙 교전 대비) — 노동은 신병이 승계
        }
      }
      long long horizon = gold_ready(true);
      long long ctr_eta = -1;
      if (need_center_train && true_center >= 0 && eta_sched(my_hq, true_center) < PINF)
        ctr_eta = eta_sched(my_hq, true_center) + (swap_body ? 0 : 1);
      bool ctr_slot = need_center_train && (war || (ctr_eta >= 0 && ctr_eta <= horizon));
      int tgt = ctr_slot ? true_center
                         : ((!war && !rusher_hold && inflight_nc < max_inflight)
                                ? pick_target(swap_body ? horizon : horizon - 1, taken)
                                : -1);
      if (tgt >= 0) {
        long long deliver = eta_sched(my_hq, tgt) + (swap_body ? 0 : 1);
        auto first_build = [&](long long g0, int extra) -> long long {
          if (parr.empty()) return 0;
          long long g = g0, inc = income_now,
                    uc = (long long)std::max(0, army_ct - doomed) + extra;
          for (long long t2 = 0; t2 <= MAX_TURN; ++t2) {
            if (t2 > 0) g += inc - (long long)UPKEEP_PER_WARRIOR * uc;
            if (g >= (long long)BASE_BUILD_COST && parr[0] <= t2) return t2;
          }
          return MAX_TURN + 1;
        };
        bool delays_pending = !parr.empty() &&
                              first_build(budget - TRAIN_COST - MOVE_COST, 1) >
                                  first_build(budget, 0);
        // 즉시선점 TRAIN-NOW = 미충전 슬롯 1차 충전 && 경합 확정만. 지원 스택은
        // JIT 게이트 케이던스(스케줄링 전환일 뿐 — 즉시훈련 아님).
        bool contested = ctr_slot && !center_charged && war;
        if (contested || (deliver >= horizon && !delays_pending)) {
          bool swapped = false;
          if (swap_body) { // #4: TRAIN + 베테랑 동시발령(스폰=이동 뒤·노동 앞 = 백필)
            budget -= TRAIN_COST;
            long long r = relay_dispatch(swap_body, tgt);
            if (r >= 0) {
              a.train_n = 1;
              swapped = true;
#ifdef CRLDBG
              if (g_crl_live)
                std::cerr << "# CRL T" << turn << " SWAPTRAIN " << swap_body->id.num
                          << " hq=" << my_hq << " tgt=" << tgt << " eta=" << r
                          << " (-1 vs pure JIT)\n";
#endif
              lab.erase(swap_body->id.num);
              if (!(tgt == true_center && war)) {
                parr.push_back(r);
                std::sort(parr.begin(), parr.end());
              } else doomed++;
              taken.insert(tgt);
              committed_sh.insert(tgt);
              inflight++;
              if (ctr_slot) center_charged = true;
              else inflight_nc++;
            } else budget += TRAIN_COST; // 발령 실패 → 순수 JIT 롤백
          }
          if (!swapped) {
            a.train_n = 1;
            budget -= TRAIN_COST;
            bs.jit_target = tgt; // 다음 턴 §5가 이 표적으로 이행
          }
        }
      }
    }
  }
  // MilitaryManager: parity-paced 상비군 (econ_open 게이트, 적 도착까지 페이싱)
  // (판정 ⑤⑩) 훈련 우선순위 = 수비 비상 > 중앙 JIT > 상비군; 경합 전쟁 중 상비군 동결.
  if (!war && hq && a.train_n == 0) {
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
#ifdef EVAL_OVERCOMMIT
    // 오버커밋 예비평가(경량): 포화+수입우위+유휴금화+위협없음이면 유휴 금화로 결진 병력을
    // 쌓도록 상비군 상한을 연다. 정밀 발사 판정은 파인셔/발사 트리거가 담당(홈예비 보존).
    {
      int my_hq_lv = hq ? hq->level : 0;
      bool oc_pre = oc_saturated(S, M, P, my_hq, my_hq_lv, inflight, 0, false) && income_dom &&
                    budget >= OC_GOLD_FLOOR && !hq_threatened && incoming_hq == 0 &&
                    ohq && ohq->side == opp;
      if (oc_pre) {
        std::vector<int> ohq_def_now;
        for (const auto &w : S.warriors)
          if (w.id.side == opp && w.hp > 0 && w.region == opp_hq) ohq_def_now.push_back(w.hp);
        int need = oc_assault_target(my_whp, ohq_def_now, ohq->turret(), OC_STRIKE_MARGIN);
        int enemy_whp = HQ_LEVELS[ohq->level].warrior_hp;
        int keep = oc_counter_need(S, M, P, opp, my_hq, my_whp, hq_turret, enemy_whp,
                                   HQ_LEVELS[ohq->level].train_cap);
        cap_army = std::max(cap_army, std::min(ARMY_HARD_MAX, need + keep));
      }
    }
#endif
    int income_after = WORK_INCOME * std::min(total_cap, army_ct + 1);
    bool sustainable = income_after - UPKEEP_PER_WARRIOR * (army_ct + 1) >= 0;
    bool room = army_ct < cap_army;
    long long floor = pol.train_reserve + (inflight > 0 ? BASE_BUILD_COST : 0); // parity 불가침
    // 상비군 기본 개방(사용자 07-07, 레거시 'HQ+2 기지' 규칙 대체): 우리 쪽 거점이
    // 전부 내 기지가 된 뒤부터 — 확장 국면과 상비군 국면을 시간으로 분리해, parity
    // 훈련이 만든 몸을 §5가 정착으로 빼가며 생기던 조기도착·수비 가용성 누수를
    // 원천 차단(93141_8 T12~T36 실측: 확장 중 상비군이 정착 파이프라인을 오염).
    // 조기 개방 = 실제 위협 관측(rush/브레이싱) 또는 뚜렷한 인구 열세
    // (opp_ct > army_ct + 3 — 순수 머릿수 미러링이던 opp_ct>army_ct를 +3 완화:
    // 상대 노동자·유휴 유닛까지 따라 뽑던 과반응 제거, 진짜 축적만 반응).
    // hq_threatened는 제외(사용자 07-07): HQ 방어는 [강제] need-bound 비상훈련·
    // 징집이 econ_open과 무관하게 전담하고, 규모가 커지면 rush가 열어준다 —
    // 1기 접근만으로 상비군 파이프라인까지 열 이유 없음.
    // '채워짐' = 진영 무관 건물 존재(사용자 07-07: 상대 선점도 소진 — 확장 여지가
    // 사라진 거점이고, 그때는 오히려 병력 국면 전환이 맞다).
    bool ourside_based = true;
    for (int sh : M.strongholds) {
      if (!on_my_side(sh)) continue;
      if (!building_at(S, sh)) { ourside_based = false; break; }
    }
    // (rushguard ⑥) 러시 교전 에피소드가 끝났으면 조기개방 트리거(rush/머릿수 열세/
    // 브레이싱)를 재점화하지 않는다 — 격퇴 직후엔 아군 전멸로 opp_ct>army_ct+3, 상대
    // 재훈련 트리클로 rush/브레이싱이 다시 참이 되기 쉬워 상비군이 확장 재개를 굶긴다.
    // 사용자 지시: 매스가 갈리면 원래 확장 최적화로 완전 복귀(정상 개방 ourside_based만).
    // 실제 재침공 방어는 [강제] need-bound 비상훈련·징집이 econ_open과 무관하게 전담.
    bool econ_open = ourside_based ||
                     (!bs.rush_done && (rush || opp_ct > army_ct + 3 || opp_bracing));
    if (room && sustainable && econ_open && budget >= TRAIN_COST + floor) {
      int n = (int)std::min<long long>(cap, (budget - floor) / TRAIN_COST);
      n = std::min(n, cap_army - army_ct);
      if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; }
    }
    // 잉여 골드 우선순위(사용자): 병력은 parity cap까지만(위 블록). 그 초과 잉여는 '놀 병력'이
    // 아니라 HQ→거점 순 레벨업에 태운다 — work_cap(income 엔진)·200턴 HQ판정 급소. 앞서도 HQ를
    // 계속 올려 코스팅으로 래더를 헌납하지 않는다(71247_5 income 동결·HQ L3 정체 교정). army_ok
    // 일 때만 HQ 투자(병력 열세 시 과투자 방지 — 레거시 hq_fund 42% 회귀 교훈). 경제가 HQ까지
    // 포화면 그때만 잉여를 병력 버퍼로. (거점 레벨업은 아래 루프가 남은 골드로 수행.)
    if (a.train_n == 0 && !hq_threatened && incoming_hq == 0 && econ_open) {
      int hq_next = (hq && hq->level < HQ_MAX_LEVEL) ? HQ_LEVELS[hq->level + 1].upgrade_cost : 0;
      bool can_ladder = hq && hq->level < HQ_MAX_LEVEL && !did_hq && army_ok &&
                        garrison_at(my_hq) >= 1 && !enemy_warrior_at(S, my_hq, me);
      if (can_ladder && budget >= hq_next + pol.econ_reserve) {
        a.upgrades.push_back(my_hq); budget -= hq_next; did_hq = true; // 잉여 → HQ 래더
      } else if (hq && hq->level >= HQ_MAX_LEVEL && army_ct < ARMY_HARD_MAX) {
        long long idle = 200 + 400LL * std::max(0, MAX_TURN - turn) / MAX_TURN; // HQ 포화 시 병력 버퍼
        if (budget >= TRAIN_COST + idle) {
          int n = (int)std::min<long long>({(long long)cap, (budget - idle) / TRAIN_COST,
                                            (long long)(ARMY_HARD_MAX - army_ct)});
          if (n > 0) { a.train_n = n; budget -= (long long)TRAIN_COST * n; }
        }
      }
    }
  }
  // 기지 레벨업 (army 뒤, 채울 노동자 있을 때만; work_cap 확장) — 전쟁 중 동결(판정 ⑩)
  if (!war && !hq_threatened)
    for (const Building *b : my_b) {
      if (b->type != BType::BASE || b->level >= BASE_MAX_LEVEL) continue;
      if (garrison_at(b->region) < b->work_cap() || enemy_warrior_at(S, b->region, me)) continue; // 적 유닛 있으면 UPGRADE=WA
      int cost = BASE_LEVELS[b->level + 1].cost;
      // C5: 기지 렙업이 HQ fund를 새게 하지 말 것(레거시 t168 base-L3 drain 회귀).
      if (budget >= cost + pol.econ_reserve + 200 + hq_fund) { a.upgrades.push_back(b->region); budget -= cost; break; }
    }

  // === hit&return 회수 (사용자 rotation): 저HP raider가 전방(상대 쪽)에 정지해 있으면
  // 홈 최근접 건물로 회수 -> HP-partition이 노동 배정(재생 불가 유닛을 생산으로 전환)
  // + 고HP로 raider 교체. 축차 소모(전방서 녹음) 대신 지속 가능한 raid 사이클. ==========
  for (auto w : avail(mobile)) {
    if (w->state != WState::STATIONARY || w->hp * 2 > my_whp) continue; // 고HP는 계속 전투
    if (on_my_side(w->region)) continue;                                 // 이미 내 쪽=partition 처리
    int home = my_hq;
    long long bd = reachable(P, w->region, my_hq) ? P.dist[w->region][my_hq] : PINF;
    for (auto b : my_b) {
      if (!reachable(P, w->region, b->region)) continue;
      long long d = P.dist[w->region][b->region];
      if (d < bd) { bd = d; home = b->region; }
    }
    issue_move(w, home);
  }
#ifdef EVAL_OVERCOMMIT
  // === 조건부 오버커밋 게이트 (econ-lead 초과 시 결정타 준비) ===============
  bool oc_engaged = false;
  int  oc_assault_need = 0;
  int  oc_home_keep = 0;
  {
    int my_hq_lv = hq ? hq->level : 0;
    bool sat = oc_saturated(S, M, P, my_hq, my_hq_lv, inflight, 0, false);
    bool idle = budget >= OC_GOLD_FLOOR;
    bool safe_posture = !hq_threatened && incoming_hq == 0;
    if (sat && income_dom && idle && safe_posture && ohq && ohq->side == opp) {
      std::vector<int> ohq_def_now;
      for (const auto &w : S.warriors)
        if (w.id.side == opp && w.hp > 0 && w.region == opp_hq) ohq_def_now.push_back(w.hp);
      int ohq_turret_now = ohq->turret();
      int enemy_whp = HQ_LEVELS[ohq->level].warrior_hp;
      int march_retrain = HQ_LEVELS[ohq->level].train_cap;
      oc_home_keep = oc_counter_need(S, M, P, opp, my_hq, my_whp, hq_turret, enemy_whp, march_retrain);
      oc_assault_need = oc_assault_target(my_whp, ohq_def_now, ohq_turret_now, OC_STRIKE_MARGIN);
      long long march_eta = reachable(P, my_hq, opp_hq) ? turns_to(P, N, my_hq, opp_hq) : PINF;
      int buildup_est = train_cap > 0 ? (oc_assault_need + train_cap - 1) / train_cap : 99;
      bool timing = oc_timing_ok(turn, march_eta, buildup_est);
      if (timing && oc_assault_need <= 64) oc_engaged = true;
    }
    bs.oc_phase = oc_engaged ? std::max(bs.oc_phase, 1) : 0;
    bs.oc_target = oc_engaged ? opp_hq : -1;
  }
#endif
  // (판정 ⑨) 경합 전쟁 중 공세 타깃선정·웨이브·잉여배치·랠리·gather 전면 동결 —
  // 잉여는 위 §5가 전부 중앙 스택으로 소진. 전쟁 종료(중앙 완공) 시 재개.
  if (war) bs.atk_target = -1;
  else {
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
    if (reachable(P, w->region, opp_hq)) hq_eta = std::max(hq_eta, turns_to(P, N, w->region, opp_hq)); // 행군 턴수(레퍼리 정확)
  int march_reinf = (ohq_tc > 0)
      ? (int)std::min<long long>({(long long)ohq_tc * hq_eta,
                                  12LL * side_income(S, opp) / TRAIN_COST, 24LL})
      : 0;
  for (int i = 0; i < march_reinf; ++i) ohq_def.push_back(ohq_whp);
  int hq_clear = min_mass_to_take(my_whp, ohq_def, ohq_turret, 64, ohq_whp, ohq_tc, ohq_sustain);
  int target = -1, need_mass = 0;
#ifdef EVAL_OVERCOMMIT
  int oc_launch_keep = 0; // >0이면 발사 시 홈에 남길 예비(내 HQ 수비 불변식)
#endif
  // HQ 커밋은 보수적으로: 행군 중 재훈련이 방어를 부풀리므로 큰 마진 + 상대 파산일 때만.
  // (공격 자살 방지 — 잉여 army는 그 외엔 홈 대기 = 수비·판정 예비.)
  bool opp_broke = side_income(S, opp) < 120;
  int opp_bases = 0;
  for (const Building *b : opp_b) if (b->type == BType::BASE) opp_bases++;
  // 적 거점 전멸(HQ만 잔존) = 적 경제 고사 → 대기하면 turn-200 무승부(73728). 후반엔 결정적으로
  // 마무리해야 하므로 커밋 마진을 축소한다(정상 상황의 과보수 마진은 원정 자살 방지용이라 유지).
  long long base_margin = (M.N >= 70) ? std::max(4, hq_clear / 2) : std::max(3, hq_clear / 3);
  if (opp_bases == 0 && turn >= 150) base_margin = std::max<long long>(2, hq_clear / 6);
  int hq_commit = hq_clear + (int)base_margin;
  bool hq_finishable = ohq && ohq->side == opp && hq_clear <= 64 &&
                       attack_force >= hq_commit && (opp_broke || attack_force >= hq_clear + ((M.N >= 70 && M.N < 100) ? 8 : ((M.N >= 100) ? 8 : 6)));
  if (hq_finishable) {
    target = opp_hq; need_mass = hq_clear;
  } else {
    long long best_score = LLONG_MIN;
    for (const Building *b : opp_b) {
      if (b->type != BType::BASE || !reachable(P, my_hq, b->region)) continue;
      // 수비 = 현 주둔 + "내 도착+2턴 내 닿는" 적 증원. "기지=정적 수비" 가정은
      // 작은 맵에서 붕괴(상대 HQ 3홉이면 need 3 웨이브가 실제 3+4를 만나 전멸
      // 반복 = N<=63 전패의 근인). 창 내 상대 HQ면 재훈련 스트림도 리졸버 반영.
      long long my_eta = turns_to(P, N, my_hq, b->region); // 주력=HQ권 근사(내 도착=레퍼리 정확 턴수)
      std::vector<int> def;
      for (const auto &w : S.warriors) {
        if (w.id.side != opp || w.hp <= 0) continue;
        if (w.region == b->region ||
            (reachable(P, w.region, b->region) && turns_to(P, N, w.region, b->region) <= my_eta + 2))
          def.push_back(w.hp);
      }
      bool hq_in_win = reachable(P, opp_hq, b->region) && turns_to(P, N, opp_hq, b->region) <= my_eta + 2;
      int win_sustain = hq_in_win ? std::min(4, (int)(4 * side_income(S, opp) / TRAIN_COST)) : 0;
      int need = min_mass_to_take(my_whp, def, b->turret(), 64, ohq_whp,
                                  hq_in_win ? ohq_tc : 0, win_sustain);
      if (need > 64) continue;
      long long d = P.dist[my_hq][b->region];
      // JIT 안전마진(사용자 요청 70074_8): 상대 증원(opp_hq->t)이 내 도착보다 늦을수록
      // 안전히 치고 복귀·다음 공격 셋업 가능 -> 그런 표적을 선호. (전방 이동은 HQ
      // 노출로 실측 붕괴 -> 이동 없이 "선택"만 안전쪽으로.)
      long long opp_reinf = reachable(P, opp_hq, b->region) ? turns_to(P, N, opp_hq, b->region) : (my_eta + 30); // 적 증원 도착 턴수
      long long safety = std::max<long long>(-6, std::min<long long>(opp_reinf - my_eta, 12));
      long long score = 80LL * b->work_cap() - 3 * d - 20LL * (int)def.size() - 10LL * b->turret() +
                        6LL * safety;
      if (score > best_score) { best_score = score; target = b->region; need_mass = std::max(need, 2); }
    }
  }
  // === 극후반 tiebreak 레이드 (사용자): turn-200 판정 = (HQ 생존, HQ 절대 HP). 풀 파괴가 안 되는
  // 극후반, 내 HQ HP를 보존하며 상대 HQ HP를 '내 것 미만'까지 깎으면 판정승(무승부→승). 절대HP라
  // 상대가 레벨 높으면 그 차이(hp_gap)만큼 더 깎아야 한다. 도착+siege가 200 전 '여유있게' 끝나도록
  // '실제 보낼 병력'의 도착 ETA로 발동(k번째 도착 후 siege 2~8턴 남는 최대 마진에서 커밋),
  // 홈 예비를 반드시 남겨 내 HQ 보존.
  int tb_home_keep = 0;
  bool tb_fire = false; // 이번 턴 판정 레이드 발화 — 진행 중 기지 웨이브보다 우선(커밋 교체)
#ifdef EVAL_OVERCOMMIT
  // === 오버커밋 발사: 포화+우위로 유휴금화 결진을 쌓았고, 홈예비를 남기고도 결정타가
  // 서면 상대 HQ에 단일 웨이브 커밋. march_reinf 인플레(hq_clear)를 그대로 존중 —
  // 병력으로 이긴다. 홈예비 미달이면 발사 안 함(무승부→패배 방지). ==================
  if (oc_engaged && !hq_finishable && ohq && ohq->side == opp && target != opp_hq) {
    long long march_eta = 0;
    for (auto w : army)
      if (reachable(P, w->region, opp_hq)) march_eta = std::max(march_eta, turns_to(P, N, w->region, opp_hq));
    bool timing = oc_timing_ok(turn, march_eta, 0); // 이미 집결군 존재 → 빌드업 0
    int send_need = hq_clear + OC_STRIKE_MARGIN;     // 인플레된 hq_clear 존중 + 마진
    int available_to_send = attack_force - oc_home_keep;
    if (timing && available_to_send >= send_need && attack_force >= send_need + oc_home_keep) {
      target = opp_hq; need_mass = hq_clear; oc_launch_keep = oc_home_keep;
      bs.oc_phase = 2; // LAUNCHED
    }
    // else: 이번 턴 발사 보류 → BUILDUP 지속. 완전 파괴가 끝내 불가하면 아래 tiebreak-raid(C 폴백)로 저하.
  }
#endif
  // hq_threatened 면제(사용자 07-07, 93380_5): 상대가 먼저 커밋한 극후반 올인이 창(tb_eta+8)
  // 내내 게이트를 잠가 우리 레이드가 영영 미발동→DRAW. 판정전에선 상대 올인=상대 홈 공동(空洞)
  // 신호이기도 하다. 홈 보존은 아래 tb_home_keep(내 HQ에 닿는 상대 야전군 전원 hold 판정+1)이 담당.
  if (!hq_finishable && ohq && ohq->side == opp && target != opp_hq && turn >= 150) {
    int my_hqhp = hq ? hq->hp : 0;
    int hp_gap = ohq->hp - my_hqhp; // '내 것 미만'까지 깎을 최소량-1 (>=0)
    if (my_hqhp <= ohq->hp) {
      // (사용자 07-07, 93481) 팬텀 요새 오판 제거 — 수비 = '관측' 기반: 현 주둔+터렛 +
      // 상대가 실제로 남길 수 있는 재훈련(골드 추정+수입 상한, 캡 12)만. march_reinf 24
      // + sustain 12 일괄 인플레는 중반 원정자살 방지용이라 판정 레이드엔 부적합 —
      // 93380/93481 실측: 상대는 전 훈련분을 우리 HQ로 스트리밍해 홈 주둔 0~1인데
      // 모델은 36팬텀 요새로 계산 → hq_clear 40+ → 영영 미발동 → DRAW.
      std::vector<int> tb_def; // 상대 HQ '현 주둔' 관측
      for (const auto &w : S.warriors)
        if (w.id.side == opp && w.hp > 0 && w.region == opp_hq) tb_def.push_back(w.hp);
      // (사용자 07-07, 93481) 창·siege 기점 = 내 HQ가 아니라 '실제 보낼 병력' 위치.
      // 커밋 풀 = 잉여 army + 징집권 노동자(수비 grind 영역 atk_hps 제외 — 필요
      // 수비수 불가침), ETA 오름차순. k번째 ETA = 필요 질량 k가 전부 도착하는 턴 —
      // 전방군(93481 T190: 91·100 = 104에서 2~3홉)이 후방 HQ 기점 수식(13홉,
      // 창 T185 마감)에 버려지던 것을 복원. 극후반 노동 수입 < 판정 HP.
      struct TBody { const Warrior *w; long long eta; bool draft; };
      std::vector<TBody> pool;
      for (auto w : army)
        if (reachable(P, w->region, opp_hq)) pool.push_back({w, turns_to(P, N, w->region, opp_hq), false});
      for (auto lw : avail(laborers)) {
        if (atk_hps.count(lw->region)) continue;
        if (!reachable(P, lw->region, opp_hq)) continue;
        pool.push_back({lw, turns_to(P, N, lw->region, opp_hq), true});
      }
      std::sort(pool.begin(), pool.end(),
                [](const TBody &x, const TBody &y) { return x.eta < y.eta; });
      std::vector<int> ehp; // 내 HQ에 닿을 수 있는 상대 잔여 야전군(홈 보존 판정)
      for (const auto &w : S.warriors)
        if (w.id.side == opp && w.hp > 0 && reachable(P, w.region, my_hq)) ehp.push_back(w.hp);
      tb_home_keep = std::min(64, min_defenders_to_hold(ehp, my_whp, hq_turret, 64)) + 1;
      int home_floor = std::max(tb_home_keep, hq_turret + 1);
      long long opp_inc = side_income(S, opp);
      long long opp_purse = std::max(0LL, S.gold[(int)opp]); // 월드모델 추정(naive=저평가 쪽)
      for (int k = 2; k <= (int)pool.size(); ++k) {
        long long eta_k = pool[k - 1].eta;
        int siege_turns = MAX_TURN - turn - (int)eta_k;
        // 1생존=siege 1/턴 보수 가정: siege_turns > hp_gap 이면 '내 것 미만' 확보 가능.
        if (siege_turns < 2 || siege_turns <= hp_gap) break; // eta 오름차순 → 이후 k는 더 늦음
        if (MAX_TURN - turn > eta_k + 8) continue;           // 최대 마진 창(조기발사 억제) 유지
        int retrain = (int)std::min<long long>({(long long)ohq_tc * (eta_k + 2),
                                                (opp_purse + opp_inc * (eta_k + 2)) / TRAIN_COST,
                                                12LL});
        int clear_k = min_mass_to_take(my_whp, tb_def, ohq_turret, 64, ohq_whp, ohq_tc, retrain);
        if (clear_k > 64) break;
        int need_k = clear_k + 2; // 여유 margin
        if (k < need_k) { // 질량 미달 → need_k번째 도착 기준으로 점프 재판정
          if (need_k > (int)pool.size()) break;
          k = need_k - 1;
          continue;
        }
        if ((int)pool.size() - need_k < home_floor) break; // 홈 예비 미달(need는 k에 비감소)
        target = opp_hq; need_mass = clear_k; tb_fire = true;
        bool conscript = false;
        for (int i = 0; i < k; ++i) if (pool[i].draft) { conscript = true; break; }
        if (conscript) { // 징집령(사용자 07-07): grind 밖 노동자 전원 웨이브 풀 편입
          for (const auto &tb : pool) if (tb.draft) army.push_back(tb.w);
          attack_force = (int)army.size();
        }
#ifdef CRLDBG
        if (g_crl_live)
          std::cerr << "# CRL T" << turn << " TBRAID k=" << k << " eta=" << eta_k
                    << " clear=" << clear_k << " retrain=" << retrain << " pool=" << pool.size()
                    << " keep=" << home_floor << " conscript=" << (conscript ? 1 : 0) << "\n";
#endif
        break;
      }
    }
  }
  // 노동자 릴레이 일반화(사용자): 목적지로 보낼 유닛 w 대신, '엄격히 더 빨리' 닿는 전방 기지
  // 노동자 L을 보내고 w가 그 기지를 1홉 내 backfill(밤명령→아침도착→저녁노동 = income 중립,
  // 자기 건물 이동비 0). 기지가 경로상이기만 해도(경유 홉수 = 직행 홉수) L의 헤드스타트만큼
  // 도착이 앞당겨져 랠리 니즈 충족·발사가 빨라진다. L은 w 이상 HP일 때만(웨이브 강도 보존 —
  // partition의 저HP 노동 원칙과 자연 양립: 부상 노동자는 스왑 안 됨). 위협 기지 노동자는 안 뺌.
  auto relay_pick = [&](const Warrior *w, int dest) -> const Warrior * {
    if (!w || used.count(w->id.num) || hq_threatened || budget < MOVE_COST) return w;
    if (!reachable(P, w->region, dest)) return w;
    long long we = turns_to(P, N, w->region, dest);
    const Warrior *L = nullptr;
    long long bestE = we;
    for (auto l : avail(laborers)) {
      if (l->hp < my_whp) continue; // 풀피 노동자만 릴레이(사용자): 피해 유닛을 랠리/웨이브에 안 넣음
                                    // (partition 원칙과 정합 — 재생불가 유닛은 노동, 풀피만 전방 baton-touch)
      int B = l->region;
      const Building *bb = building_at(S, B);
      if (!bb || bb->side != me || atk_hps.count(B) || enemy_warrior_at(S, B, me)) continue;
      if (!reachable(P, B, dest) || !reachable(P, w->region, B)) continue;
      if (turns_to(P, N, w->region, B) > 1) continue; // backfill 1홉 = income 중립
      long long le = turns_to(P, N, B, dest);
      if (le < bestE) { bestE = le; L = l; }
    }
    if (!L) return w;
    if (w->region != L->region) issue_move(w, L->region); // backfill(자기 건물 = 무료)
    else used.insert(w->id.num);                          // 이미 그 기지면 고정만
    return L;
  };
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
      auto rit = bs.route_to.find(w.id.num); // 경유 중이면 최종 목적지로 판정(경유점 해산 방지)
      int loc = rit != bs.route_to.end() ? rit->second
                : (w.state == WState::MOVING) ? w.target : w.region;
      if (loc == bs.atk_target) committed++;
    }
    if (tgt_live && committed > 0) wave_in_flight = true;
    else bs.atk_target = -1; // resolved -> free to re-commit
  }
  // 전원 즉시 발사(주력이 HQ 스폰 공유로 대체로 동위치). eta-스케줄 출발(분산 대기
  // 후 동시수렴)은 실측 악화(4-5-1 -> 3-7-0): 대기 유닛이 수비에 재차출돼 웨이브가
  // 새고, 출발창을 놓친 유닛이 영구 이탈 -> 미달 전력 도착. 즉시 발사가 강건.
  // 위협 중에도 공세발사(사용자): army=avail은 강제수비 grind-window draft(생산→잉여→노동병까지
  // in-window 소집)가 HQ need를 이미 확보한 뒤의 잉여 → offense가 HQ 수비를 침범 못 함. HQ는 그
  // grind가 담당하므로 잉여 웨이브를 위협 중 홀드할 이유 없음(공격 랠리분대=잉여 소집 마지막 단계).
  // (잔여 리스크: grind가 빠른 러쉬를 과소투영 + 전군이 먼 공성에 이동잠금이면 회수 불가 — grind
  // 투영 정확도 문제이지 잉여 침범 아님.)
  bool may_launch = true;
  // tb_fire는 wave_in_flight 무시(사용자 07-07, 93481 ③): 극후반 기지 웨이브가 늘 떠 있어
  // 판정 레이드가 창을 소진하던 것 차단 — 커밋을 opp_hq로 교체(이동 중 몸은 규칙상 그대로,
  // 정지 잉여만 재발사; 다음 턴 커밋 추적은 104행 유닛을 따라감).
  if (target >= 0 && need_mass <= 64 && (!wave_in_flight || tb_fire) && attack_force >= need_mass && may_launch) {
    // 전방 raider(사용자 요청, 검증된 인간 전략): 무른(need≤raid_cap) JIT-안전 기지엔
    // 소수 raid party(raid_cap=5, 후반 7)만 sortie해 연속 파괴 + hit&return, 주력은
    // 홈 수비. 저HP 귀환병은 partition이 노동 회수·고HP 교체(rotation 자동). 방어된
    // 기지/HQ 끝내기는 전군 웨이브. 목표에 가까운 순으로 party명 발사(전방 요충 효과).
    int raid_cap = 5 + (turn >= 130 ? 2 : 0);
    // raid = 상대 경제를 흔드는 소수 파괴전. 상대가 기지에서 앞서거나(스노볼) 내가
    // 병력 잉여(전군 웨이브 필요 없음)일 때만 소수 sortie — 그 외엔 전군 웨이브가
    // 더 결정적(실측: 무조건 raid는 1347d 64->59, 무승부↑). 무른 표적에 한함.
    bool snowball = (int)opp_b.size() > (int)my_b.size();
    bool have_surplus = attack_force >= need_mass + raid_cap;
    bool soft_raid = (target != opp_hq) && (need_mass <= raid_cap) && (snowball || have_surplus);
    int party = soft_raid ? std::min<int>((int)army.size(), std::max(need_mass + 1, raid_cap))
                          : (int)army.size();
#ifdef EVAL_OVERCOMMIT
    if (oc_launch_keep > 0) party = std::min(party, (int)army.size() - oc_launch_keep);
#endif
    std::vector<const Warrior *> sorted(army.begin(), army.end());
    std::sort(sorted.begin(), sorted.end(), [&](const Warrior *x, const Warrior *y) {
      long long dx = reachable(P, x->region, target) ? P.dist[x->region][target] : PINF;
      long long dy = reachable(P, y->region, target) ? P.dist[y->region][target] : PINF;
      return dx < dy;
    });
    // 통합 경유 계산 (사용자 요청): 직행 경로의 중간 방어(터렛+주둔)를 흡수하고도 도착 병력이
    // 표적 need_mass 이상이면 우회 없이 직행한다(시간 이득 — 71247_3 T116 48경유 승리). 못 미치면
    // issue_attack의 우회로 병력을 보존. 지역 임계(path_threat≤2) 대신 "경유해도 이기나"로 판단.
    // 대표 경로위협 = 표적 최근접(선두) 유닛의 path_threat(웨이브가 funnel하는 공유 병목).
    int sent = std::min<int>(party, (int)sorted.size());
    long long route_threat = sorted.empty() ? 0 : path_threat(sorted[0]->region, target);
    bool direct_wins = (long long)sent - route_threat >= (long long)need_mass;
    for (int i = 0; i < sent; ++i) {
      const Warrior *u = relay_pick(sorted[i], target); // 전방 노동자 스왑: 헤드스타트만큼 도착 앞당김
      if (direct_wins) issue_route(u, target);   // 경유해도 이김 → 최속(기지 경유 홉단축 포함)
      else             issue_attack(u, target);  // 손실 크면 우회로 보존
    }
    bs.atk_target = target;
  }
  // === 잉여군 배치 ①단계 — 거점 만석(사용자: 먼저 거점에 주둔). 위협 건물 필요 수비수는 pin.
  // 만석은 income 엔진이자 기지 렙업 조건. 웨이브 중에도 계속(경제는 안 멈춘다 — !hq_threatened만).
  if (!hq_threatened) {
    for (auto &kv : atk_hps) { // 위협 건물 필요 수비수 pin (배치/발사에서 제외)
      const Building *b = building_at(S, kv.first);
      if (!b || kv.first == my_hq) continue;
      int need = min_defenders_to_hold(kv.second, my_whp, b->turret(), 64) + 1;
      int pin = 0;
      for (auto w : avail(mobile)) {
        if (pin >= need) break;
        if (w->region == kv.first) { used.insert(w->id.num); pin++; }
      }
    }
    for (const Building *b : my_b) { // work_cap 빈 기지를 최근접 유휴로 채움
      int slots = b->work_cap() - garrison_at(b->region);
      for (int k = 0; k < slots; ++k) {
        const Warrior *w = nullptr;
        long long best = PINF;
        for (auto m2 : avail(mobile)) {
          if (m2->region == b->region || !reachable(P, m2->region, b->region)) continue;
          long long d = P.dist[m2->region][b->region];
          if (d < best) { best = d; w = m2; }
        }
        if (!w) break;
        issue_move(w, b->region);
      }
    }
  }
  // === rally 산정 — 잉여를 모을 '맵 전체에 유효한 집결지': 안전 아군 건물(우리편/중앙, 위협 없음)
  // 중 적 기지들까지 거리 '합'이 최소인 곳 = 어느 약체든 짧게 치고 짧게 회수하는 중앙-전방 허브.
  int stage = -1;
  {
    // 내가 거점을 먹어 전진할수록 rally도 '안전·전방'으로 전진해야 마무리 출발이 빠르다
    // (73728: rally가 정중앙 52에 고정 → 전군이 후방서 모여 먼 적HQ 104를 못 끝냄 = 무승부).
    // 후보 = 위협 없는(atk_hps 미포함) 내 건물. 점수 = 남은 적 기지 거리합(없으면 적HQ 거리) 최소
    // = 최전방·유리 우선. 전방(dmh>doo) 후보는 내가 거점 우위일 때만(열세면 후방 유지 — 홈 노출 방지).
    bool base_ahead = (int)my_b.size() > (int)opp_b.size();
    long long best = PINF;
    for (const Building *b : my_b) {
      long long dmh = reachable(P, my_hq, b->region) ? P.dist[my_hq][b->region] : PINF;
      long long doo = reachable(P, opp_hq, b->region) ? P.dist[opp_hq][b->region] : PINF;
      if (doo >= PINF || atk_hps.count(b->region)) continue; // 위협받는 곳 제외
      if (dmh > doo && !base_ahead) continue;                // 전방 rally는 거점 우위일 때만
      long long sum = 0;
      int cnt = 0;
      for (const Building *e : opp_b)
        if (e->type == BType::BASE && reachable(P, b->region, e->region)) {
          sum += P.dist[b->region][e->region];
          cnt++;
        }
      long long score = cnt > 0 ? sum : doo; // 적 기지 없으면 적HQ 근접(전방)으로 폴백
      if (score < best) { best = score; stage = b->region; }
    }
  }
  // === 로밍 harass 웨이브(사용자, 순이득 확인): 잉여를 '한 거점'에 모아 리졸버 need(가까운 약체 2개
  // 클리어 질량 합)만큼 쌓이면 그 거점에서 '한 번에' 발사(같은 지점 출발 → 동시 도착; 짤짤이·따로
  // 도달 근절). 발사점은 가장 전방 아군 건물부터 검사 → 방금 점령한 전방 거점의 생존 뭉치도 발사점이
  // 돼 두 번째 약체로 롤(동선 최적화·두 거점 연속 타격). 저HP 생존병은 hit&return이 후방/점령거점
  // 노동자로 차출. ourside 만석 전엔 미발동. 표적=발사점 최근접 약체.
  // 위협 중에도 잉여 harass 발사(사용자): staged=avail(강제수비 draft가 HQ need 확보 후 잉여) + 수비
  // grind-window로 HQ 충분 → HQ 비축 대신 적당히 공세. hq_threatened 게이트 제거(발사는 staged≥need로 바운드).
  // 랠리 트리거(사용자 모델): 확장이 끝나야 랠리가 선다 — 확장과 랠리는 시간적으로 배타라 충돌
  // 자체가 성립 안 한다. 게이트 = 우리쪽 만석(경제 완성) 또는 상대 집결(opp_bracing=요격 대비)
  // + 확장 미진행(expand_done: inflight==0, 정착민 도보/대기 없음). 이 게이트 전엔 gather/발사
  // 미발동 → 도착·골드 대기 중인 정착민을 잉여로 오인·강탈하던 확장/랠리 충돌이 원천 소멸.
  bool ourside_full = true;
  for (const Building *b : my_b)
    if (on_my_side(b->region) && garrison_at(b->region) < b->work_cap()) { ourside_full = false; break; }
  bool expand_done = (inflight == 0);
  bool rally_fill = (ourside_full || opp_bracing) && expand_done; // gather(집결·요격 겸용)
  // 발사(harass)는 만석 완성 + 확장종료일 때만 — 상대집결만으론 요격 예비만 채우고 선제공격은 안 함.
  if (!wave_in_flight && bs.atk_target < 0) {
    if (ourside_full && expand_done) {
      // 발사점 = 내 건물(my_b) + '이번 턴 파괴돼 빈 요새에 내 고HP 생존병 주둔' 지점. 후자는
      // 점령→건설→발사점등록의 1턴 지연(월드모델엔 새 기지가 다음 턴 등장) 때문에 롤을 못 하고
      // gather로 회수되던 문제(73502 T93: 57 부수고 UPGRADE 57 했는데 my_b 미등록→생존병 52 회수)를
      // 해결한다. EconomyManager가 정착민 1기를 이미 used(그 자리 기지 건설)해도 남은 고HP 생존병이
      // 여기서 다음 약체로 즉시 롤 → 57→65 연쇄 성립(발사점 전방이라 아래 need는 '한 거점분').
      std::vector<int> lps;
      std::set<int> lp_seen;
      for (const Building *b : my_b) { lps.push_back(b->region); lp_seen.insert(b->region); }
      for (auto w : avail(mobile)) {
        int r = w->region;
        if (lp_seen.count(r) || !is_sh(r) || building_at(S, r) || w->hp * 2 <= my_whp) continue;
        lps.push_back(r); lp_seen.insert(r); // 빈 요새 위 고HP 생존병 = 즉석 발사점
      }
      std::sort(lps.begin(), lps.end(), [&](int x, int y) {
        long long dx = reachable(P, x, opp_hq) ? P.dist[x][opp_hq] : PINF;
        long long dy = reachable(P, y, opp_hq) ? P.dist[y][opp_hq] : PINF;
        return dx < dy; // 가장 전방(opp_hq 근접) 발사점 먼저 → 롤 우선
      });
      auto clear_mass = [&](const Building *b) {
        std::vector<int> def;
        for (const auto &w : S.warriors)
          if (w.id.side == opp && w.hp > 0 && w.region == b->region) def.push_back(w.hp);
        return min_mass_to_take(my_whp, def, b->turret(), 64);
      };
      auto gar_of = [&](const Building *b) {
        int g = 0;
        for (const auto &w : S.warriors)
          if (w.id.side == opp && w.hp > 0 && w.region == b->region) g++;
        return g;
      };
      // 표적 가치/비용 (사용자): 거리최근접 대신 가치(상대 수입차단 골드등가 + 기지격파 300)/비용(예상
      // HP손실=투입−생존 ×120 + 이동잠금 홉×수입 + 마진 1기)로 정렬. HP손실 항이 순손실 레이드를 자연
      // 기각(리졸버 '승리'여도 교환 손해면 sc<1로 제외). from = 발사 기준 구역.
      auto vc_score = [&](int from, const Building *b) -> double {
        std::vector<int> def;
        int lab = 0;
        for (const auto &w : S.warriors)
          if (w.id.side == opp && w.hp > 0 && w.region == b->region) { def.push_back(w.hp); lab++; }
        int need = min_mass_to_take(my_whp, def, b->turret(), 64);
        if (need > 64) return -1.0; // 못 먹음
        int surv = region_fight_survivors(std::vector<int>(need, my_whp), def, b->turret());
        int lost = std::max(0, need - surv);
        double value = (double)WORK_INCOME * std::min(lab, b->work_cap()) *
                           std::min(std::max(1, MAX_TURN - turn), 23) +
                       BASE_BUILD_COST; // 기지 격파 = 재훈련 불가 영구손실
        double cost = (double)lost * TRAIN_COST +
                      (double)turns_to(P, N, from, b->region) * WORK_INCOME + TRAIN_COST;
        return value / cost;
      };
      for (int lp : lps) {
        std::vector<const Warrior *> staged; // 이 발사점에 모인 고HP 뭉치(동시 출발분)
        for (auto w : avail(mobile))
          if (w->region == lp && w->hp * 2 > my_whp) staged.push_back(w);
        if (staged.empty()) continue;
        // b1/b3 = lp에서 가치/비용 상위 1·2위 (gar>6·못먹음·순손실 sc<1 제외)
        std::vector<std::pair<double, const Building *>> cand;
        for (const Building *b : opp_b) {
          if (b->type != BType::BASE || !reachable(P, lp, b->region) || gar_of(b) > 6) continue;
          double sc = vc_score(lp, b);
          if (sc >= 1.0) cand.push_back({sc, b});
        }
        if (cand.empty()) continue;
        std::sort(cand.begin(), cand.end(), [](auto &x, auto &y) { return x.first > y.first; });
        const Building *b1 = cand[0].second;
        // b2 = b1에서 가치/비용 최고(b1 제외) — 롤 이어치기 대상.
        const Building *b2 = nullptr;
        double b2best = 0;
        for (const Building *b : opp_b) {
          if (b->type != BType::BASE || b == b1) continue;
          if (!reachable(P, b1->region, b->region) || gar_of(b) > 6) continue;
          double sc = vc_score(b1->region, b);
          if (sc >= 1.0 && sc > b2best) { b2best = sc; b2 = b; }
        }
        // sizing(사용자: 2거점분으로 축소 — b3 복귀버퍼 제거): 후방 랠리 발사 = 두 거점분(b1+b2, 롤
        // 이어치기까지) / 전방 발사점(롤) = 한 거점분. b3 clear_mass는 더 이상 규모에 안 더한다.
        long long lp_dmh = reachable(P, my_hq, lp) ? P.dist[my_hq][lp] : PINF;
        long long lp_doo = reachable(P, opp_hq, lp) ? P.dist[opp_hq][lp] : PINF;
        int need = clear_mass(b1);
        if (lp_dmh <= lp_doo && b2) need += clear_mass(b2); // 후방 랠리 = 2거점 sizing
        need = std::max(2, need + 1);
        if ((int)staged.size() >= need) { // 발사점에 need만큼 모임 → 전량 동시 발사(b1로)
          for (int i = 0; i < need && i < (int)staged.size(); ++i)
            issue_attack(staged[i], b1->region);
          bs.atk_target = b1->region; // 커밋. b1 파괴 후 롤/복귀는 다음 턴 재평가(생존 뭉치=전방 발사점)
          break;                      // 한 발사점(한 웨이브)만
        }
      }
    }
  }
  // === 잉여 집결/회수(gather) — 고HP 잉여를 rally로 모은다. 후방뿐 아니라 '전방 점령거점에 남은
  // 생존병'도 rally로 회수(눌러앉음 방지) → 사이클 성립: 치고 → rally 복귀 → 재집결 → 다시 치기.
  // (roll은 위 harass가 먼저 처리 = 이동중이라 여기 회수 대상 아님. 저HP는 hit&return 노동 차출.)
  // 웨이브 중에도 계속. HQ 정적 비축 금지(사용자 165945): 잉여를 HQ에 눌러앉히지 않고 전량 랠리로.
  // 적절한 랠리는 요격 겸용이라 HQ 위협도 타이밍 좋게 커버 — 실제 HQ 수비 need는 위 [강제] draft가
  // 이미 담당(used)하므로 여기로 오는 건 진짜 잉여. 트리거 = rally_fill(만석 or 상대집결 + 확장종료):
  // 확장 진행 중(inflight>0)엔 gather 미발동 → 대기 정착민 강탈·확장 방해 소멸(사용자 모델: 확장 끝=랠리 시작).
  // 공성 중(적 건물 위) 유닛만 회수 금지(아래).
  if (rally_fill && stage >= 0 && stage != my_hq) {
    for (auto w : avail(mobile)) {
      if (w->hp * 2 <= my_whp) continue;         // 저HP=hit&return 노동 차출
      if (w->region == stage) continue;          // 이미 rally
      // 공성 중(적 건물 위)이면 회수 금지 — 수비만 죽이고 안 부순 채 복귀하는 버그 방지.
      // 건물 파괴 후엔 빈 요새 → EconomyManager가 우리 기지 건설, 그 다음 잉여만 회수(73314_5).
      const Building *wb = building_at(S, w->region);
      if (wb && wb->side == opp) continue;
      const Warrior *u = relay_pick(w, stage); // 전방 노동자 스왑(랠리 니즈 조기 충족)
      issue_route(u, stage);
    }
  }
  } // end if(!war) — 공세·랠리·잉여배치 동결 블록
  // 안전망: UPGRADE 무효화 방지 — (1) 같은 구역 중복(WA), (2) 적 유닛 있는 구역(건설·렙업·힐
  // 모두 '적 유닛 존재 시 UPGRADE 불가'=WA=즉시 몰수). 73728 T197 반칙패: 점령 기지 101에 상대
  // 재훈련병이 다시 진입했는데 렙업 UPGRADE 발령 → RIGHT_WIN WA. 모든 UPGRADE 경로를 여기서 최종 차단.
  {
    std::set<int> seen;
    std::vector<int> u;
    for (int r : a.upgrades)
      if (!enemy_warrior_at(S, r, me) && seen.insert(r).second) u.push_back(r);
    a.upgrades.swap(u);
  }
  return a;
}

// ===================== StrategyManager: 잉여배분 롤아웃 (Phase 3) =============
// 자유 수(Policy)를 K턴 롤아웃(정확 시뮬 재사용)해 value function으로 채점 -> 최선 선택.
// 강제 수(수비/끝내기)는 policy-무관하게 decide 안에서 결정 -> 롤아웃은 잉여배분만 탐색.
// 상대는 고정 BALANCED(우리 복사본=미러 아님) -> 무승부 편향·맹점공유 회피(설계 디테일1 MAP판).
static Cmds strategy_manager(const SimState &S0, const GameMap &M, const Paths &P,
                             const RelayTab &RT, int turn, BotState &bs) {
  const Side me = M.my_side, opp = opposite(me);
  const int K = std::max(1, std::min(MAX_TURN - turn, (M.N >= 70 && M.N < 100) ? 8 : 10)); // credible horizon
  const bool mid_map = M.N >= 70 && M.N < 100;
  // rushguard 전담 수비 중: 수순이 policy-무관 강제라 롤아웃 무의미 → 생략(시간 절약).
  // (감지 자체는 live decide 안에서 일어나므로 감지 턴에는 평소처럼 롤아웃이 돈다.)
  if (bs.rush_guard) {
#ifdef CRLDBG
    g_crl_live = true;
#endif
    Cmds live = decide(S0, M, P, RT, turn, bs, mid_map ? presets::BALANCED_MID : presets::BALANCED);
#ifdef CRLDBG
    g_crl_live = false;
#endif
    return live;
  }
  const Policy cands[] = {
      mid_map ? presets::BALANCED_MID : presets::BALANCED,
      mid_map ? presets::TURTLE_MID : presets::TURTLE,
      mid_map ? presets::RUSH_MID : presets::RUSH,
      mid_map ? presets::EXPAND_MID : presets::EXPAND
  };
  const Policy oppol = mid_map ? presets::BALANCED_MID : presets::BALANCED;
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
      Cmds cme = decide(s, M, P, RT, turn + d, dme, pol);
      Cmds copp = decide(s, oppM, P, RT, turn + d, dopp, oppol);
      Cmds &cl = (me == Side::LEFT) ? cme : copp;
      Cmds &cr = (me == Side::LEFT) ? copp : cme;
      step(s, M, P, cl, cr);
      if (!building_at(s, hq_region_of(M, me)) || !building_at(s, hq_region_of(M, opp))) break;
    }
    double e = eval_state(s, M, me, turn + K);
    if (e > best) { best = e; bestP = &pol; }
  }
#ifdef CRLDBG
  g_crl_live = true; // 라이브 결정만 프린트(위 롤아웃은 침묵)
#endif
  Cmds live = decide(S0, M, P, RT, turn, bs, *bestP); // 실턴: live 예측 + 최선 policy
#ifdef CRLDBG
  g_crl_live = false;
#endif
  return live;
}

// ===================== live main (clean-room) ================================
int main() {
  GameMap M;
  SimState S;
  parse_init(M, S);
  Paths P = calculate_paths(M);
  RelayTab RT = build_relay_tables(M, P); // centerRelapse #2: 홉·경유 테이블 사전계산
  BotState bs;
  int turn;
  while (read_turn_start(turn)) {
    g_turn_start = std::chrono::steady_clock::now(); // 턴 시간예산 시작
    Cmds a = strategy_manager(S, M, P, RT, turn, bs);
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
#elif !defined(UNIT_TEST)

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

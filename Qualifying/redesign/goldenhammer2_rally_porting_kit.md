# goldenhammer2 Rally Porting Kit

목적: `86801.cpp` 계열 코드에 `goldenhammer2.cpp`의 유의미한 기본 동작 변화만 이식하기 위한 준비 문서.

이식 대상은 `RALLY_CONVERT` 계열이다. `EVAL_OVERCOMMIT`는 기본 빌드에서 꺼진 실험 코드라 제외한다.

## 1. 맵 크기 기반 window 함수 추가

상수 영역, 예를 들면 `BASE_HEAL_COST` 아래에 추가한다.

```cpp
static int rally_convert_window_by_map_size(int N) {
  if (N <= 70) return 6;
  if (N < 85) return 7;
  return 8;
}
```

의미: 랠리 후보가 실제 공격으로 전환될 수 있어야 하는 최대 허용 턴 수를 맵 크기에 따라 바꾼다.

`goldenhammer2` 원본은 6턴 고정이지만, 다운로드 폴더의 19개 로그 분석에서는 6턴 고정이 중대형 맵의 유효 후보를 지나치게 많이 자르는 것으로 나타났다. 1차 이식안은 보수적으로 `6/7/8` 3단계만 둔다.

## 2. 선택 계측 코드 추가

`BotState` 정의 직후에 넣는다. 없어도 전략 동작에는 영향이 없고, `-DRALLY_AUDIT`로 빌드할 때만 랠리 후보 필터링을 관찰한다.

```cpp
#ifdef RALLY_AUDIT
struct RallyAuditRecord {
  int turn = 0;
  int raw_stage = -1;
  int stage = -1;
  int raw_candidates = 0;
  int pass_candidates = 0;
  bool rally_fill = false;
  int rally_moves = 0;
};
static std::vector<RallyAuditRecord> g_rally_audit;
#endif
```

## 3. `decide()` 내부 helper 추가

`decide()` 안에서 기존 랠리 산정 블록 바로 앞에 추가한다. 위치 기준은 “거점 만석 배치”가 끝난 직후, `int stage = -1;`가 나오기 전이다.

필요한 기존 변수/함수:

- `S`, `P`, `N`, `turn`
- `me`, `opp`, `my_hq`, `opp_hq`
- `my_whp`, `ohq`, `hq_clear`
- `mobile`, `opp_b`
- `avail(mobile)`
- `reachable`, `turns_to`
- `min_mass_to_take`, `region_fight_survivors`
- `WORK_INCOME`, `TRAIN_COST`, `BASE_BUILD_COST`, `MAX_TURN`, `PINF`

```cpp
  auto enemy_clear_mass = [&](const Building *b) {
    std::vector<int> def;
    for (const auto &w : S.warriors)
      if (w.id.side == opp && w.hp > 0 && w.region == b->region) def.push_back(w.hp);
    return min_mass_to_take(my_whp, def, b->turret(), 64);
  };
  auto enemy_garrison_count = [&](const Building *b) {
    int g = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == opp && w.hp > 0 && w.region == b->region) g++;
    return g;
  };
  auto base_value_cost_score = [&](int from, const Building *b) -> double {
    if (!b || b->type != BType::BASE || !reachable(P, from, b->region)) return -1.0;
    std::vector<int> def;
    int lab = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == opp && w.hp > 0 && w.region == b->region) { def.push_back(w.hp); lab++; }
    int need = min_mass_to_take(my_whp, def, b->turret(), 64);
    if (need > 64) return -1.0;
    int surv = region_fight_survivors(std::vector<int>(need, my_whp), def, b->turret());
    int lost = std::max(0, need - surv);
    double value = (double)WORK_INCOME * std::min(lab, b->work_cap()) *
                       std::min(std::max(1, MAX_TURN - turn), 23) +
                   BASE_BUILD_COST;
    double cost = (double)lost * TRAIN_COST +
                  (double)turns_to(P, N, from, b->region) * WORK_INCOME + TRAIN_COST;
    return value / cost;
  };
  const int RALLY_CONVERT_WINDOW = rally_convert_window_by_map_size(M.N);
  auto rally_ready_by = [&](int cand, int need) -> long long {
    if (need <= 0) return 0;
    std::vector<long long> eta;
    for (auto w : avail(mobile)) {
      if (w->hp * 2 <= my_whp || !reachable(P, w->region, cand)) continue;
      eta.push_back(turns_to(P, N, w->region, cand));
    }
    std::sort(eta.begin(), eta.end());
    if ((int)eta.size() < need) return PINF;
    return eta[need - 1];
  };
  auto rally_can_convert = [&](int cand) -> bool {
    bool has_enemy_base = false;
    std::vector<std::pair<double, const Building *>> targets;
    auto slow_window_guard = [&](long long need_window, long long cand_dmh,
                                 long long cand_doo, double sc) {
      if (need_window <= 6) return true;
      if (cand_dmh > cand_doo && sc < 1.15) return false;
      return true;
    };
    for (const Building *b : opp_b) {
      if (b->type != BType::BASE) continue;
      has_enemy_base = true;
      if (!reachable(P, cand, b->region)) continue;
      if (turns_to(P, N, cand, b->region) > RALLY_CONVERT_WINDOW) continue;
      if (enemy_garrison_count(b) > 6) continue;
      double sc = base_value_cost_score(cand, b);
      if (sc < 1.0) continue;
      targets.push_back({sc, b});
    }
    if (!targets.empty()) {
      std::sort(targets.begin(), targets.end(), [](auto &x, auto &y) { return x.first > y.first; });
      const Building *b1 = targets[0].second;
      const Building *b2 = nullptr;
      double b2best = 0;
      for (const Building *b : opp_b) {
        if (b->type != BType::BASE || b == b1) continue;
        if (!reachable(P, b1->region, b->region) || enemy_garrison_count(b) > 6) continue;
        double sc = base_value_cost_score(b1->region, b);
        if (sc >= 1.0 && sc > b2best) { b2best = sc; b2 = b; }
      }
      long long cand_dmh = reachable(P, my_hq, cand) ? P.dist[my_hq][cand] : PINF;
      long long cand_doo = reachable(P, opp_hq, cand) ? P.dist[opp_hq][cand] : PINF;
      int need = enemy_clear_mass(b1);
      if (cand_dmh <= cand_doo && b2) need += enemy_clear_mass(b2);
      need = std::max(2, need + 1);
      long long ready = rally_ready_by(cand, need);
      long long attack_eta = turns_to(P, N, cand, b1->region);
      long long need_window = std::max(ready, attack_eta);
      if (need <= 64 && need_window <= RALLY_CONVERT_WINDOW &&
          slow_window_guard(need_window, cand_dmh, cand_doo, tg.first))
        return true;
    }
    if (!has_enemy_base && ohq && ohq->side == opp && hq_clear <= 63 &&
        reachable(P, cand, opp_hq) &&
        turns_to(P, N, cand, opp_hq) <= RALLY_CONVERT_WINDOW) {
      long long cand_dmh = reachable(P, my_hq, cand) ? P.dist[my_hq][cand] : PINF;
      long long cand_doo = reachable(P, opp_hq, cand) ? P.dist[opp_hq][cand] : PINF;
      long long ready = rally_ready_by(cand, std::max(2, hq_clear + 1));
      long long attack_eta = turns_to(P, N, cand, opp_hq);
      long long need_window = std::max(ready, attack_eta);
      return need_window <= RALLY_CONVERT_WINDOW &&
             slow_window_guard(need_window, cand_dmh, cand_doo, 1.20);
    }
    return false;
  };
```

`slow_window_guard`는 맵 크기 때문에 7~8턴 후보를 허용할 때만 작동하는 안전장치다. 6턴 이하 후보는 원래 `goldenhammer2`의 빠른 전환 후보이므로 그대로 통과시킨다. 7~8턴 후보가 전방(`cand_dmh > cand_doo`)인데 가치/비용 점수가 간신히 `1.15` 미만이면, 병력을 멀리 묶는 리스크가 보상보다 크다고 보고 차단한다.

## 4. 랠리 후보 산정 블록 수정

기존 `int stage = -1;` 랠리 산정 블록에 아래 세 가지를 더한다.

### 4-1. stage 선언 직후 계측 변수

```cpp
#ifdef RALLY_AUDIT
  int audit_raw_stage = -1, audit_raw_candidates = 0, audit_pass_candidates = 0;
#endif
```

### 4-2. `best` 선언 직후 계측용 best

```cpp
#ifdef RALLY_AUDIT
    long long audit_raw_best = PINF;
#endif
```

### 4-3. `score` 계산 직후 필터 추가

기존에는 `score < best`면 바로 stage 후보가 됐다. 이제 그 전에 “이 지점이 공격으로 전환 가능한가”를 검사한다.

```cpp
#ifdef RALLY_AUDIT
      audit_raw_candidates++;
      if (score < audit_raw_best) { audit_raw_best = score; audit_raw_stage = b->region; }
#endif
      if (!rally_can_convert(b->region)) continue;
#ifdef RALLY_AUDIT
      audit_pass_candidates++;
#endif
```

그 뒤 기존 코드는 그대로 둔다.

```cpp
      if (score < best) { best = score; stage = b->region; }
```

## 5. 기존 harass helper 중복 제거 또는 공통 helper 사용

`goldenhammer2`에서는 기존 harass 블록 안의 로컬 helper였던 `clear_mass`, `gar_of`, `vc_score`를 바깥으로 끌어올려 공통화했다.

이식 시 harass 블록 안에 같은 역할의 helper가 이미 있다면 제거하고 아래처럼 이름을 교체한다.

```cpp
clear_mass(b)  -> enemy_clear_mass(b)
gar_of(b)      -> enemy_garrison_count(b)
vc_score(a, b) -> base_value_cost_score(a, b)
```

이 작업을 하지 않으면 컴파일은 될 수 있지만 같은 계산이 중복되어 유지보수가 꼬인다.

## 6. rally move 계측 추가

`rally_fill` 계산 뒤에 추가한다.

```cpp
  int rally_moves = 0;
```

기존 gather 발령부:

```cpp
      issue_route(u, stage);
```

를 아래처럼 바꾼다.

```cpp
      if (issue_route(u, stage)) rally_moves++;
```

그리고 `decide()` 반환 직전, UPGRADE 안전망 전에 계측 저장을 추가한다.

```cpp
#ifdef RALLY_AUDIT
  g_rally_audit.push_back({turn, audit_raw_stage, stage, audit_raw_candidates,
                           audit_pass_candidates, rally_fill, rally_moves});
#endif
```

## 7. 이식 후 기대 효과

기존 `86801` 계열 랠리는 “거리합이 좋은 안전 아군 건물”을 고른다. 이식 후에는 그 후보가 맵 크기별 window 안에 적 기지 또는 상대 HQ 공격으로 전환될 수 있는지도 확인한다.

따라서 병력이 의미 없는 곳에 모이는 상황, 후반에 병력은 있는데 타격으로 이어지지 않아 무승부가 나는 상황을 줄이는 것이 핵심 효과다.

## 8. 19개 로그 기반 window 분석

분석 대상: `/Users/seongho/Downloads/The Last Day` 아래 19개 referee 로그.

분석 방식: 각 로그를 재생하면서 양쪽 시점에서 랠리 후보를 평가했다. 후보별로 “병력이 해당 랠리포인트에 모이는 데 필요한 턴”과 “그 지점에서 가치/비용상 유효한 적 기지 또는 상대 HQ까지 공격 전환하는 턴”을 계산하고, 둘 중 더 큰 값을 해당 후보의 최소 필요 window로 보았다.

요약:

| 맵 크기 구간 | 관찰 경향 | 권장 window |
|---|---|---:|
| `N <= 70` | p75가 대체로 5~6턴. 6턴 고정이 대부분의 유효 후보를 살린다. | 6 |
| `71 <= N < 85` | 샘플은 적지만 6턴보다 큰 후보가 나타난다. 중간 완충이 필요하다. | 7 |
| `85 <= N < 100` | p75가 6~8턴으로 올라간다. 6턴 고정은 후보를 많이 막는다. | 8 |
| `N >= 100` | p75가 7턴 근처, p90이 8턴 근처다. 큰 맵에서도 8턴이면 대부분의 실전 후보를 커버한다. | 8 |

대표 수치:

- `N=57~69`: p75 `5~6`, p90 `6~7`.
- `N=75`: p75 `6`, p90 `8`, p95 `9`.
- `N=87`: p75 `8`, p90 `9`, 6턴 이하 후보는 `1180/1875`.
- `N=97`: p75 `8`, p90 `9`, 6턴 이하 후보는 `318/534`.
- `N=101`: p75 `7`, p90 `8`, 6턴 이하 후보는 `1132/1521`.
- `N=103`: p75 `7`, p90 `8`, 6턴 이하 후보는 `2415/3388`.

해석:

6턴 고정은 “곧바로 때릴 수 있는 집결지”라는 의도에는 맞지만, 큰 맵에서는 전방 기동 자체가 길어져 좋은 랠리포인트도 7~8턴 전환 거리에 놓이는 경우가 많다. 그래서 작은 맵은 6턴을 유지하고, 중간 맵은 7턴, 큰 맵은 8턴으로 완화하는 것이 현재 로그셋 기준 가장 안전하다.

너무 크게 늘리지 않는 이유:

window를 9~10턴까지 열면 p95 후보까지 더 많이 살릴 수 있지만, 랠리가 “공격 준비”가 아니라 “먼 대기 지점”으로 변질될 위험이 있다. 현재 목표는 랠리의 공격성을 유지하면서 6턴 고정의 과필터링만 줄이는 것이므로 8턴을 상한으로 둔다.

## 9. 가상 정책 검증 결과

동일 조건 self-play A/B가 어렵기 때문에, 19개 로그의 각 턴 상태를 복원한 뒤 같은 상태에서 세 정책을 동시에 평가했다.

- `fixed6`: 원본 goldenhammer2 방식, 6턴 고정.
- `byN`: 이 문서의 `6/7/8` 맵 크기 기반 window.
- `fixed8`: 모든 맵에서 8턴 허용.

실제 로그 진행은 바꾸지 않고, 각 상태에서 어떤 stage가 선택될 수 있는지만 비교했다. 따라서 승률 검증은 아니며, 랠리 판단 품질에 대한 오프라인 압박 테스트다.

전체 결과:

```text
views=6250
raw stage 후보 있음=6024
fixed6 선택=2153
byN 선택=2237
fixed8 선택=2246
byN이 fixed6 대비 새로 살린 stage=84
fixed8이 fixed6 대비 새로 살린 stage=93
byN 추가 후보 중 위험 후보=10
byN 추가 후보 중 전방 후보=31
```

해석:

`byN`은 `fixed6`보다 stage를 84번 더 살렸고, `fixed8`의 93번과 거의 비슷하다. 즉 작은 맵은 보수적으로 유지하면서도 큰 맵에서는 8턴 개방 효과의 대부분을 얻는다.

하지만 추가로 살아난 후보 중 10개는 위험 후보로 분류됐다. 특히 `N=107`에서는 추가 후보 8개 중 4개가 위험 후보였다. 따라서 `byN` 함수만 넣는 것보다는, 7~8턴 후보에 한해 `slow_window_guard`를 함께 넣는 편이 더 안전하다.

위험 후보 판정은 다음 중 하나를 만족하는 경우로 잡았다.

- 후보가 전방이고(`cand_dmh > cand_doo`) 전환 window가 느림(`>= 8`).
- 가치/비용 점수가 낮고(`sc < 1.15`) 전환 window가 느림(`>= 8`).

이 기준은 보수적인 필터다. 좋은 후보 일부도 잘릴 수 있지만, 현재 목적은 승률을 증명하는 것이 아니라 “큰 맵 window 완화가 수비 템포를 망칠 위험”을 줄이는 것이다.

## 10. 이식 검증 체크리스트

1. 일반 빌드가 컴파일되는지 확인한다.
2. `-DRALLY_AUDIT` 빌드도 컴파일되는지 확인한다.
3. `pass_candidates == 0`인 턴이 너무 많으면 `rally_convert_window_by_map_size()`의 경계값을 완화해본다.
4. 랠리 이동이 과도하게 줄어들어 병력이 HQ에 고이면 필터가 너무 강한 것이다.
5. 무승부 로그에서 후반 stage가 더 전방/공격 가능 지점으로 잡히는지 확인한다.
6. 큰 맵에서 랠리 이동이 너무 먼 곳으로 새면 `N >= 100` 구간을 8에서 7로 되돌려 비교한다.
7. `slow_window_guard` 적용 후 `byN`이 추가로 살리는 후보가 너무 줄면 `1.15` 문턱을 `1.05~1.10`으로 낮춰 비교한다.

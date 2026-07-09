# rushguard recovery / base_plan_health 적용 전 충돌 검토

대상 코드:

- `7-8-15.cpp`

검토 대상 수정:

- `rushguard recovery`: 초반 러시 방어 이후 경제/병력 재개방이 닫혀 장기 무행동으로 빠지는 상황 방지
- `base_plan_health`: 최근 기지 손실과 전선 상태를 보고 위험한 경제 목적 재건/확장을 제한

이 문서는 바로 구현하기 위한 설계서가 아니라, 적용 시 기존 코드에서 발생할 수 있는 버그, 충돌, 판단 오류를 미리 정리한 문서다. 특히 두 로직의 발동 근거를 명확히 하여 “감으로 막는 로직”이 되지 않게 하는 것을 목표로 한다.

## 현재 코드의 관련 구조

### rushguard

관련 위치:

- `BotState` rush 상태: `rush_guard`, `rush_rally`, `rush_engaged`, `rush_done`, `rush_intruders`
- rush 감지/해제/복구: `decide()` 초반 rushguard 블록
- `econ_open` 조기 개방 제한: `rush_done`이 true이면 rush/열세/brace에 의한 조기 개방이 꺼짐

현재 흐름:

1. `turn <= 10`에 상대 5기 이상 뭉침을 보면 `rush_guard = true`.
2. 감지 즉시 `mission`, `route_to`, `jit_target`, `atk_target` 등을 초기화.
3. rushguard 중에는 일반 확장/업글/공세를 모두 멈추고 `return a`.
4. 침입자 명부가 비고 상대 HQ 스테이징이 4기 미만이면 해제.
5. 실제 교전이 있었으면 `rush_done = true`.
6. 이후 `econ_open`은 정상 개방 조건인 `ourside_based`가 되기 전까지 조기 개방이 막힐 수 있다.

위 구조는 “러시를 막은 뒤 다시 무리하게 상비군 조기 개방이 켜지는 것”을 막기 위한 안전장치다. 따라서 recovery를 넣을 때 `rush_done` 자체를 무력화하면 기존 의도를 깨뜨릴 수 있다.

### base build / expansion

관련 위치:

- `EconomyManager`: 빈 요새 위에 내 유닛이 있으면 즉시 `UPGRADE`로 기지 건설
- `centerRelapse` 미션/정착 파이프라인: `mission`, `jit_target`, `inflight`, `committed_sh`
- `rally_fill`: 확장이 끝나야 rally/gather가 열림
- 방어 draft: `atk_hps`에 잡힌 내 건물은 need-bound로 방어하거나 포기

현재 흐름:

1. 위협 예측으로 `atk_hps`를 만든다.
2. 기존 건물 위협은 수비 블록에서 need-bound로 처리한다.
3. 그 후 빈 요새 위 내 유닛은 목적 구분 없이 기지로 건설한다.
4. 확장 미션은 `mission`과 `committed_sh`로 추적된다.
5. `inflight > 0`이면 rally/gather가 닫히는 구조라, 확장과 집결은 의도적으로 분리되어 있다.

따라서 base_plan_health는 단순히 “최근 깨진 구역이면 건설 금지”로 넣으면 안 된다. 정착 파이프라인과 rally 개방 타이밍이 동시에 흔들릴 수 있다.

## rushguard recovery의 판단 근거

recovery가 발동해야 하는 근거는 “rushguard가 있었다”가 아니다. 러시를 막은 뒤 **실제로 운영 재개에 실패한 상태**여야 한다.

권장 발동 조건:

```text
rushguard episode가 실제 교전까지 갔음
AND rushguard 해제 후 1~25턴 이내
AND 생존 기지 수 <= 1
AND 최근 유효 명령이 부족하거나, 훈련/건설/확장 미션이 없음
AND 강제 HQ 방어가 이미 처리됐거나, 이번 턴 recovery가 강제 방어보다 먼저 실행되지 않음
```

각 조건의 이유:

- `rushguard episode가 실제 교전까지 갔음`: 오발 감지로 1~2턴 지연된 경우까지 recovery를 켜면 초반 빌드가 망가진다.
- `해제 후 25턴 이내`: 영구 모드가 되면 `rush_done`의 의도와 충돌한다.
- `생존 기지 <= 1`: 경제가 닫힌 상태를 잡기 위한 핵심 조건이다.
- `유효 명령 부족`: 단순히 기지 1개인 초반 정상 상태와 OSS형 데드락을 구분한다.
- `강제 HQ 방어 우선`: recovery가 수비 TRAIN/MOVE를 방해하면 안 된다. 구현 위치는 강제 수비 블록 이후, 자유 경제/확장 블록 이전이 가장 안전하다.

권장 행동:

```text
1. 후방 안전 기지 건설 또는 재건
2. HQ 주변 최소 훈련
3. 첫 기지 노동 복구
4. mission/jit_target이 막혀 있으면 재시작
```

금지해야 할 행동:

```text
1. 전방 확장 강제
2. 상대 HQ 공격 강제
3. center_war 무시
4. rush_guard 활성 중 일반 스케줄링 재개
```

## rushguard recovery 적용 시 충돌 위험

### 1. `rush_done` 의도 무력화

위험:

```text
rush_done 이후 조기 econ_open을 다시 열어버림
-> 원래 의도한 “러시 격퇴 후 과잉 상비군 재개방 방지”가 깨짐
-> 초반 확장/경제가 병력 훈련으로 다시 밀릴 수 있음
```

방지:

```text
rush_done은 유지한다.
recovery는 econ_open 자체를 true로 바꾸지 않는다.
대신 recovery 전용 최소 행동만 우회 허용한다.
```

즉 다음은 위험하다.

```cpp
if (recovery_mode) econ_open = true; // 위험
```

더 안전한 방식:

```cpp
if (recovery_mode) {
  // 후방 안전 기지, 최소 훈련만 별도 블록에서 허용
}
```

### 2. rushguard 활성 중 일반 스케줄링과 충돌

현재 rushguard 중에는 블록 끝에서 `return a`로 일반 로직을 전부 멈춘다. recovery를 이 블록 안에 넣으면 rushguard가 아직 끝나기 전 일반 행동이 섞일 수 있다.

위험:

```text
rush_guard == true
AND recovery_mode == true
-> 확장/훈련/방어가 중복 실행
-> used 중복, budget 중복 차감, mission 꼬임
```

방지:

```text
recovery는 rush_guard가 false가 된 다음 턴부터만 작동.
```

### 3. HQ 방어 TRAIN과 recovery TRAIN 충돌

현재 HQ 방어 수비 블록은 `a.train_n`을 선점한다. recovery가 별도 훈련을 넣으면 같은 턴 TRAIN이 덮이거나 과다 계산될 수 있다. 따라서 recovery는 강제 수비 블록보다 앞에서 실행하면 안 된다.

위험:

```text
수비 TRAIN n 발생
recovery TRAIN m 추가
-> a.train_n 덮어쓰기 또는 budget 불일치
```

방지:

```text
recovery 훈련은 a.train_n == 0일 때만 허용.
또는 기존 수비 TRAIN이 있으면 recovery는 훈련 대신 건설/이동만 고려.
recovery 블록은 강제 수비 이후, EconomyManager 즉시 건설 이전에 두는 것이 가장 단순하다.
```

### 4. mission 초기화 후 복구 타이밍 충돌

rushguard 감지 시 기존 `mission`, `route_to`, `jit_target`, `atk_target`이 초기화된다. recovery가 너무 빠르게 새 mission을 만들면, 아직 귀환 중인 유닛이나 rush rally 복귀와 충돌할 수 있다.

위험:

```text
해제 턴에 HQ 생존자를 첫 기지로 돌려보냄
같은 턴 recovery가 다른 기지 mission을 만듦
-> 노동 복구 유닛과 정착 유닛 선발이 엇갈림
```

방지:

```text
해제 당일에는 recovery flag만 세우고,
실제 recovery 행동은 다음 decide부터 수행.
```

## base_plan_health의 판단 근거

base_plan_health의 목적은 기지 건설을 줄이는 것이 아니라, **경제 목적 확장과 방어/미끼 목적 재건을 구분**하는 것이다.

최근 파괴구역을 무조건 막으면 안 되는 이유:

```text
1. 방어 chokepoint 재건일 수 있음
2. 상대 이동을 지연시키는 미끼일 수 있음
3. 중앙/길목 점유가 승리 패턴일 수 있음
4. 승리 로그에도 위험 확장이 존재함
```

따라서 기지 후보는 최소 세 가지로 분류해야 한다.

```text
ECON_BASE: 수입/전진 rally/장기 점유 목적
DEF_BASE: HQ/핵심기지 보호, choke 방어 목적
SAC_BASE: 지연/미끼 목적, 방어 병력 투입 금지
```

권장 판단 근거:

```text
recent_loss: 최근 20~30턴 같은 구역 또는 인접 구역 기지 파괴 여부
dist_balance: 내 HQ 거리와 상대 HQ 거리 비교
enemy_eta: 현재 상대 병력이 해당 구역에 도달하는 최소 턴
my_eta: 내 보강 병력이 해당 구역에 도달하는 최소 턴
can_garrison: 1~2턴 안에 주둔/보강 가능 여부
expected_hold_turns: 터렛+주둔으로 몇 턴 버틸지 추정
income_payback: 300골드를 수입으로 회수할 수 있는 예상 턴
strategic_block: 그 구역이 내 HQ/핵심기지 경로를 실제로 늦추는지
```

안전한 1차 의사결정:

```text
recent_loss가 false:
  기존 로직 유지

recent_loss가 true:
  DEF 조건 통과:
    DEF_BASE로 허용
  DEF 조건 실패 but enemy_eta를 1~2턴 늦추는 SAC 가치 있음:
    SAC_BASE로 허용하되 방어 draft 금지
  그 외:
    ECON 재건 차단 또는 큰 감점
```

## base_plan_health 적용 시 충돌 위험

### 1. EconomyManager의 즉시 건설과 충돌

현재 빈 요새 위 내 유닛은 바로 기지를 짓는다. 여기에 차단을 넣을 때 `used` 처리와 `mission` 정리가 엇갈리면 코드 상태가 꼬일 수 있다.

위험:

```text
건설은 막았는데 used 처리됨
-> 유닛이 그 턴 아무것도 못 함

건설은 막았는데 mission은 도착 완료로 삭제됨
-> 다음 턴 재평가 대상에서 빠지거나, 같은 턴 Pass B에서 빈 요새 대기 정착민으로 다시 커밋 처리됨

건설은 허용했는데 mission이 남음
-> 이미 기지가 생겼는데 같은 목적지 mission이 계속 유지됨
```

방지:

```text
block build:
  a.upgrades에 넣지 않음
  budget 차감 없음
  used 처리 없음
  mission은 의도에 따라 명확히 처리
    - 단순 보류라면 mission을 유지하거나 다음 턴 재평가 가능하게 둠
    - 포기라면 mission 삭제 + released 처리
    - 같은 턴 Pass B가 다시 committed로 잡는지 확인

allow build:
  기존과 동일하게 a.upgrades, budget 차감, used 처리
  해당 dest mission은 이후 cleanup에서 building_at(dest) 또는 도착 조건으로 정리됨
```

중요한 코드 순서:

```text
EconomyManager 즉시 건설
-> centerRelapse mission cleanup
-> Pass A/Pass B committed 집계
```

따라서 base_plan_health가 EconomyManager에서 건설을 막으면, 뒤쪽 cleanup과 Pass B가 같은 유닛을 어떻게 볼지 반드시 같이 확인해야 한다.

### 2. `centerRelapse` 중앙 커밋과 충돌

중앙은 이 코드에서 매우 특별하게 다뤄진다. `alwaysCenter` 설계상 두 번째 건설은 중앙을 강하게 선호하고, `center_war` 중에는 경제/공세가 동결된다.

위험:

```text
recent_loss 때문에 중앙 재건/중앙 커밋을 막음
-> 기존 centerRelapse 설계 붕괴
-> 상대 중앙 선점/경합 패배
```

방지:

```text
true_center는 base_plan_health 일반 ECON 차단에서 제외하거나,
별도 CENTER_BASE 판단으로 분리한다.
center_war 중에는 base_plan_health가 일반 경제 로직을 열면 안 된다.
```

### 3. `inflight`와 rally/gather 타이밍 충돌

현재 `rally_fill = (ourside_full || opp_bracing) && expand_done`이고, `expand_done = inflight == 0`이다. base_plan_health가 확장을 차단하면 `inflight`가 줄어 rally가 빨리 열릴 수 있다.

좋은 효과:

```text
위험 확장 중단
-> 병력 집결 빨라짐
-> qwerty/AI류 손실 감소 가능
```

나쁜 효과:

```text
원래 필요한 확장까지 중단
-> rally가 너무 빨리 열림
-> 수입 기반이 작아진 상태로 공격/집결
-> 기존 승리 패턴 약화
```

방지:

```text
base_plan_health는 전체 확장 중단이 아니라 특정 후보 감점/차단이어야 한다.
후방 안전 후보가 있으면 대체 후보로 보낸다.
```

### 4. DEF_BASE와 방어 draft 충돌

DEF_BASE로 지은 기지는 지킬 의도가 있다. 하지만 현재 방어 draft는 `atk_hps`에 잡힌 기존 건물 위협만 본다. 방금 지을 예정인 기지의 미래 위협과 주둔 계획은 반영하지 않는다.

위험:

```text
DEF_BASE로 판단해 건설
하지만 같은 턴/다음 턴 보강이 안 됨
-> 즉시 파괴

반대로 SAC_BASE가 다음 턴 이후 atk_hps에 잡히면 방어 draft가 병력을 보냄
-> 미끼 기지에 병력까지 헌납
```

방지:

```text
DEF_BASE:
  can_garrison_soon 통과 시만 허용
  필요하면 건설 직후 rally/배치에서 우선 보강

SAC_BASE:
  해당 region을 sacrifice set에 기록
  다음 턴 이후 방어 draft/pin 대상에서 제외
```

단, sacrifice set을 잘못 쓰면 정말 지켜야 할 기지를 버릴 수 있으므로 지속 기간을 짧게 둬야 한다. HQ와 core 후방 기지는 sacrifice 대상에서 무조건 제외해야 한다.

### 5. HQ 업그레이드/HQ fund와 충돌

기지 재건을 막으면 골드가 남고, 그 골드가 HQ 업그레이드로 갈 수 있다. 이건 판정패 방지에는 좋지만, 초중반에는 병력/확장 부족을 만들 수 있다.

위험:

```text
base_plan_health가 확장 차단
-> budget 잔류
-> HQ 업그레이드 조건 충족
-> 병력/기지 수 부족한데 HQ에 돈이 감
```

현재 코드에는 `deep_behind`와 `army_ok`가 있지만, 특정 상황에서는 충분하지 않을 수 있다.

방지:

```text
base_plan_health로 확장을 차단한 턴에는,
그 골드가 곧바로 재량 HQ 업그레이드로 흐르지 않도록 reason을 구분한다.

예:
blocked_frontier_expand 때문에 남은 골드는
  후방 안전 기지 / 회복 훈련 / 수비 reserve 우선
```

### 6. 위험 확장을 막다가 공격 발사점을 잃는 오류

승리 로그에서는 전방 기지가 공격 발사점, rally, hit&return 회수 지점으로 기능한다. 최근 손실이 있었다는 이유로 전방 건설을 막으면 공격 사이클이 늦어질 수 있다.

위험:

```text
전방 기지 미건설
-> stage가 후방에 고정
-> 적 기지/HQ까지 거리 증가
-> hq_eta/march_reinf가 커짐
-> hq_finishable이 더 안 열림
```

방지:

```text
전방이지만 공격 발사점 가치가 큰 후보는 ECON 차단 대상과 분리한다.
다만 can_garrison_soon 또는 immediate_roll 가능성이 있어야 한다.
```

## 두 로직이 동시에 적용될 때의 상호작용

### 좋은 상호작용

```text
rushguard 후 recovery가 최소 기지 재건을 시도
base_plan_health가 위험 전방 재건을 막음
-> 후방 안전 기지로 복구
-> OSS형 무행동 감소
```

### 나쁜 상호작용

```text
rushguard 후 기지가 0개
recovery가 기지 재건 필요 판단
base_plan_health가 최근 손실 때문에 후보를 모두 차단
-> recovery가 켜졌는데 할 행동이 없음
-> 기존 데드락이 유지
```

방지:

```text
recovery_mode에서는 base_plan_health를 완화하되,
후방 안전 후보만 선택한다.

즉:
normal mode: 위험 후보 차단
recovery mode: 최소 1개 후방 안전 기지는 강제 허용
```

### 또 다른 나쁜 상호작용

```text
recovery가 훈련을 강제
base_plan_health가 확장을 차단
-> 병력은 늘지만 기지가 없음
-> upkeep 증가, 수입 회복 실패
```

방지:

```text
recovery 우선순위는 훈련보다 후방 안전 기지.
단 HQ 즉시 위협이면 수비 TRAIN 우선.
```

## 기존 코드 기준 상세 충돌 분석

### A. recovery 삽입 위치별 위험

`rushguard recovery`는 넣는 위치에 따라 의미가 달라진다.

#### A-1. rushguard 블록 내부에 넣는 경우

위치 후보:

```text
bs.rush_guard 처리 블록 안쪽
```

위험도: 매우 높음.

이유:

- rushguard 활성 중에는 마지막에 `return a`로 일반 스케줄링을 모두 막는다.
- 이 블록 안에서 recovery를 실행하면 아직 러시가 끝나지 않았는데 경제/확장 행동이 섞인다.
- `rush_guard` 상태의 의도는 “수비 외 전면 동결”이므로 recovery와 목적이 다르다.

발생 가능한 버그:

```text
1. rushguard 중 첫 기지 복구와 recovery 후방 기지 건설이 같은 턴에 동시 시도
2. budget이 rushguard 훈련과 recovery 건설에 이중 사용
3. used 처리된 HQ/기지 유닛을 recovery가 다시 선발
4. rush_rally 복귀 유닛이 recovery 정착민으로 재선발
5. rushguard의 return a 때문에 recovery가 일부 상태만 바꾸고 실제 행동은 출력되지 않는 반쪽 상태
```

판단:

```text
금지. recovery는 rush_guard == false 이후에만 실행해야 한다.
```

#### A-2. 강제 수비 블록보다 앞에 넣는 경우

위치 후보:

```text
위협 예측/수비 draft 이전
```

위험도: 높음.

이유:

- 이 코드에서 HQ/기지 방어는 강제 수비 블록이 먼저 `a.train_n`, `used`, `budget`을 선점하는 구조다.
- recovery가 먼저 훈련/이동을 실행하면, 정작 HQ 방어가 필요한 유닛과 골드를 잃을 수 있다.

발생 가능한 버그:

```text
1. recovery TRAIN이 a.train_n을 차지해 HQ 방어 TRAIN 불가
2. recovery MOVE가 mobile/laborer를 used 처리해 방어 draft 실패
3. recovery 건설로 budget이 줄어 수비 훈련 shortfall 발생
4. hq_threatened 계산 전이라 실제 위협이 있는데 안전하다고 오판
```

판단:

```text
금지. recovery는 atk_hps 생성과 강제 수비 이후에 판단해야 한다.
```

#### A-3. EconomyManager 즉시 건설 이후에 넣는 경우

위치 후보:

```text
EconomyManager 빈 요새 즉시 건설 이후
```

위험도: 중간~높음.

이유:

- 이미 빈 요새 위 유닛이 기지를 지었고 `used` 처리된 뒤라 recovery가 선택할 수 있는 유닛이 줄어든다.
- 위험한 전방 재건을 막고 싶어도 이미 지은 뒤가 된다.
- 남은 budget과 `built_here`가 recovery 의도와 다르게 계산된다.

발생 가능한 버그:

```text
1. recovery가 막아야 할 전방 재건이 먼저 실행됨
2. recovery 후방 기지 후보 유닛이 이미 used 처리됨
3. built_here 증가로 income_now/gold_ready가 recovery 이전 상태와 다르게 계산됨
```

판단:

```text
recovery와 base_plan_health의 행동 변경은 EconomyManager 즉시 건설 전에 들어가는 편이 맞다.
단, 강제 수비 이후여야 한다.
```

#### A-4. 권장 위치

가장 안전한 위치:

```text
강제 수비 블록 종료
-> 자유/경제 블록 기본 지표 계산
-> rush/econ_open/army/base 지표 산정
-> recovery/base_plan_health 판단
-> EconomyManager 즉시 건설
-> HQ ladder / centerRelapse / MilitaryManager
```

주의:

- `true_center`, `enemy_race_eta`, `relay_dispatch` 등 centerRelapse 내부 helper를 쓰려면 그 helper들이 정의된 뒤여야 한다.
- 반대로 EconomyManager보다 뒤로 가면 빈 요새 즉시 건설을 차단할 수 없다.
- 따라서 실제 구현에서는 helper 의존성을 최소화한 `safe_recovery_base_candidate`를 EconomyManager 앞에 별도로 계산하는 것이 안전하다.

### B. `rush_done`과 `econ_open`의 의미 충돌

현재 `econ_open`은 다음 의미를 가진다.

```text
ourside_based
OR
(!rush_done && (rush || military behind || opp_bracing))
```

즉 `rush_done == true`는 “러시 이후 조기 상비군 개방을 다시 켜지 않는다”는 강한 상태다.

recovery가 이 구조를 잘못 건드리면 다음 버그가 난다.

#### B-1. recovery가 `econ_open = true`를 강제

위험:

```text
1. 상비군 훈련 블록이 열림
2. §5 norob 경계가 바뀌어 확장 선발 방식이 달라짐
3. HQ ladder 잉여 업글 조건이 열림
4. 회복 목적이 아니라 정상 경제/군사 전체가 열림
```

특히 `econ_open`은 단순 훈련 플래그가 아니다. 정착 선발 방식, JIT, 상비군, HQ ladder까지 공유하는 상위 게이트다.

판단:

```text
recovery는 econ_open을 바꾸면 안 된다.
recovery 전용 행동만 별도 예외로 실행해야 한다.
```

#### B-2. recovery가 `rush_done = false`로 되돌림

위험:

```text
1. 러시 격퇴 후 다시 opp_bracing/behind로 조기 상비군 개방
2. 러시 방어로 약해진 경제가 병력 생산에 더 밀림
3. 기존 rushguard 설계 의도와 정면 충돌
```

판단:

```text
rush_done은 과거 episode 기록으로 유지한다.
필요하면 별도 recovery_until만 추가한다.
```

### C. base_plan_health와 EconomyManager의 상태 불일치

현재 EconomyManager는 매우 단순하다.

```text
avail(mobile) 중
빈 stronghold 위에 있고
적 유닛 없고
budget >= 300이면
즉시 UPGRADE + built_here + budget 차감 + used 처리
```

base_plan_health가 여기에 끼어들면 다음 네 가지 상태를 동시에 맞춰야 한다.

```text
a.upgrades
built_here
budget
used
```

#### C-1. 차단했는데 `built_here`만 넣는 오류

발생:

```text
base_plan_health가 build를 차단했지만 built_here에 region을 넣음
```

결과:

```text
1. 실제 기지는 안 짓는데 이후 로직은 지은 것으로 계산
2. income_now가 과대평가될 수 있음
3. Pass A/Pass B가 해당 지역을 committed에서 제외
4. 다음 턴에도 복구가 안 되는 유령 건설 상태 발생
```

#### C-2. 차단했는데 `used` 처리하는 오류

발생:

```text
건설은 안 했는데 정착민을 used 처리
```

결과:

```text
1. 그 턴 유닛이 방어/이동/다른 정착에 쓰이지 못함
2. Pass B에서도 제외되어 inflight로도 잡히지 않음
3. 빈 요새 위에서 아무 행동 없이 턴을 버림
```

#### C-3. 허용했는데 `used` 처리하지 않는 오류

발생:

```text
기지는 지었는데 건설자를 used 처리하지 않음
```

결과:

```text
1. 같은 턴 gather/relay/공격에 다시 선발될 수 있음
2. 레퍼리상 UPGRADE와 MOVE를 같은 유닛이 동시에 할 수 없어 WA 위험
3. 후속 used 기반 불변식 붕괴
```

#### C-4. 허용했는데 `built_here`를 누락하는 오류

발생:

```text
a.upgrades에는 넣었지만 built_here에 넣지 않음
```

결과:

```text
1. 같은 턴 다른 유닛이 같은 region을 다시 커밋할 수 있음
2. Pass A/Pass B가 새 기지 예정지를 빈 요새로 보고 committed 처리
3. 중복 UPGRADE 안전망은 최종 출력만 막고 내부 inflight/parr 계산은 이미 오염
```

판단:

```text
base_plan_health가 EconomyManager에 들어간다면,
allow/block 양쪽에서 a.upgrades/built_here/budget/used를 한 덩어리로 다뤄야 한다.
```

### D. mission cleanup / Pass B와의 충돌

현재 순서:

```text
EconomyManager 즉시 건설
-> mission cleanup
-> relay continuation
-> Pass A mission committed
-> Pass B 미션 없는 빈 요새 대기 정착민 committed
-> inflight 계산
```

이 순서 때문에 “건설 차단”은 단순히 그 턴 UPGRADE 하나를 막는 것이 아니다.

#### D-1. mission 도착 정착민의 건설 차단

상황:

```text
정착 mission 유닛이 목적지에 도착
base_plan_health가 그 지역 건설을 차단
```

가능한 후속:

```text
1. cleanup에서 w.region == dest라 mission 삭제
2. 그 유닛은 mission이 없어짐
3. Pass B에서 빈 요새 위 대기 정착민으로 다시 committed 처리될 수 있음
4. inflight가 유지되어 rally_fill이 계속 닫힐 수 있음
```

이건 때로는 좋다. “건설 대기” 의도를 표현할 수 있기 때문이다. 하지만 위험 후보를 포기하려는 의도였다면 나쁘다.

필요한 구분:

```text
BLOCK_WAIT:
  이번 턴 돈/상황 때문에 보류. mission 또는 Pass B committed 유지 허용.

BLOCK_ABANDON:
  위험해서 포기. mission 삭제 + released 처리 + 같은 턴 Pass B 재커밋 방지 필요.
```

#### D-2. `released`와의 충돌

현재 `released`는 enemy race에서 진 비중앙 mission을 당일 재선발하지 않기 위한 장치다.

base_plan_health가 위험 후보를 포기할 때도 유사한 “당일 재선발 금지”가 필요하다.

위험:

```text
base_plan_health가 후보 A를 차단
-> mission 삭제
-> 같은 턴 dispatch_to가 다시 후보 A를 선택
-> 차단/재선발 루프
```

방지:

```text
base_plan_health로 포기한 유닛 또는 지역을 released/taken/blocklist에 반영해야 한다.
```

### E. `inflight`와 `gold_ready` 오염

`inflight`는 단순 표시가 아니라 여러 게이트에 쓰인다.

영향 범위:

```text
1. rally_fill의 expand_done
2. MilitaryManager floor = train_reserve + (inflight > 0 ? 300 : 0)
3. oc_saturated
4. max_inflight / inflight_nc 확장 직렬화
5. gold_ready의 parr 큐
```

base_plan_health가 후보를 차단했는데도 Pass B가 committed로 잡으면:

```text
inflight가 유지됨
-> rally/gather 닫힘
-> 상비군 floor가 300 올라감
-> 기지 건설비 예약 때문에 훈련/HQ 업글 타이밍 변화
```

반대로 차단하면서 committed에서도 완전히 빼면:

```text
inflight가 0이 됨
-> rally/gather가 빨리 열림
-> 원래 확장 대기해야 할 유닛이 집결/공격으로 빠짐
-> 경제 기반이 약한 상태에서 공격 사이클 시작
```

판단:

```text
base_plan_health는 차단 종류를 inflight 정책과 같이 정해야 한다.

BLOCK_WAIT:
  inflight 유지 가능.

BLOCK_ABANDON:
  inflight 제거 + 당일 재선발 금지.

DEF/SAC 허용:
  built_here/used 정상 처리.
```

### F. true_center / center_war 특수 처리 누락 위험

`true_center`는 일반 stronghold와 다르다.

현재 코드 특성:

```text
1. R 후보에서 true_center는 제외
2. slot_active로 별도 관리
3. war 중 중앙은 무제한 스택 가능
4. center_war 중 일반 경제/공세는 동결
5. 중앙은 enemy_warrior_at이 있어도 일부 커밋 집계에서 예외
```

base_plan_health가 일반 후보 판정으로 true_center를 막으면 위험하다.

발생 가능한 버그:

```text
1. alwaysCenter 의도 붕괴
2. 중앙 커밋이 늦어져 초반 경합 패배
3. center_charged가 false로 남아 JIT TRAIN이 반복
4. 중앙 war 중인데 recovery/base_plan_health가 비중앙 경제를 열어 판정 ⑨⑩ 위반
```

판단:

```text
true_center는 별도 CENTER_BASE로 다뤄야 한다.
normal ECON 차단 조건을 그대로 적용하면 안 된다.
war == true이면 recovery/base_plan_health는 일반 비중앙 행동을 열면 안 된다.
```

### G. SAC_BASE가 방어 로직과 충돌하는 실제 양상

SAC_BASE는 “지어도 버릴 기지”다. 하지만 현재 방어 로직은 기지 목적을 모른다.

발생 순서:

```text
T: SAC_BASE 건설
T+1: 상대가 해당 기지로 이동 중이라고 예측됨
T+1: atk_hps[region] 생성
T+1: 방어 draft가 need 계산
T+1: 지킬 수 있다고 판단하면 병력을 보냄/핀 처리
```

이러면 SAC_BASE가 DEF_BASE처럼 행동한다.

필수 보완:

```text
sacrifice_until[region] > turn이면
  def_order에서 해당 region 제외
  배치① pin 제외
  stage 후보 점수 감점 또는 제외
```

단, 제외 범위는 조심해야 한다.

```text
절대 제외 금지:
  my_hq
  HQ 인접 core 방어 기지
  현재 내 주력 rally/stage
  true_center 중 war 상태
```

### H. recovery와 HQ ladder의 간접 충돌

recovery가 위험 확장을 막으면 budget이 남는다. 이 budget은 바로 HQ ladder나 hq_fund로 흐를 수 있다.

현재 HQ 업글 경로:

```text
1. EconomyManager 직후 HQ ladder 선제 판단
2. hq_fund 예약
3. MilitaryManager 후 잉여 HQ ladder
4. 기지 레벨업은 hq_fund를 보고 제한
```

위험:

```text
base_plan_health가 300골드 지출 차단
-> budget이 HQ cost + reserve 충족
-> HQ 업글 발생
-> recovery가 원한 후방 기지/병력 복구가 밀림
```

특히 `deep_behind`는 “상대 기지 수와 상대 병력 수가 둘 다 앞설 때”만 막는다. 기지는 적지만 수입이 무너진 상황, 또는 병력은 비슷하지만 기지 0개인 상황은 놓칠 수 있다.

방지:

```text
recovery_mode 또는 blocked_frontier_expand가 true인 턴에는
재량 HQ 업글 조건에 recovery_reserve를 추가한다.

단, opp_hq_lvl >= my_hq_lvl인 패리티 추격까지 막으면 판정패가 늘 수 있으므로
막을 대상은 “재량 선제 업글”로 제한해야 한다.
```

### I. debug 계측 자체의 오판 위험

BOTDBG를 넣을 때도 주의가 필요하다. 잘못된 계측은 잘못된 판단을 강화한다.

주의할 점:

```text
1. recent_loss는 현재 S만으로 알 수 없으므로 BotState에 이전 건물 기록이 필요
2. enemy_eta는 pred 기반인지 현재 위치 기반인지 구분해야 함
3. my_eta는 relay/mvt 기준인지 레퍼리 turns_to 기준인지 구분해야 함
4. expected_hold_turns는 상대 증원/재훈련을 과소평가하면 DEF_BASE 오판
5. income_payback은 upkeep과 worker fill 가능성을 같이 봐야 함
```

추천 로그:

```text
# BOTDBG T42 base_eval r=57 kind=ECON recent=1 decision=BLOCK_ABANDON enemyEta=2 myEta=5 hold=1 payback=20 reason=enemy_faster_recent_loss
# BOTDBG T14 recovery active=1 reason=rush_done_low_base base=12 action=SAFE_REBUILD train=0 blockEconOpen=1
# BOTDBG T91 sac_def_skip r=48 until=96 incoming=3 reason=sacrifice_region
```

## 구현 전 추가 상태 변수 위험

두 로직을 넣으려면 `BotState`에 새 상태가 필요할 가능성이 높다. 새 상태는 오래 남으면 독이 된다.

권장 상태:

```text
int recovery_until
int last_rush_end_turn
std::map<int,int> destroyed_base_recent_until
std::map<int,int> sacrifice_until
std::set<int> blocked_this_turn
```

각 상태의 만료 규칙:

```text
recovery_until:
  rushguard 해제 후 20~25턴, 또는 base >= 2가 되면 종료

destroyed_base_recent_until:
  파괴 관측 후 20~30턴

sacrifice_until:
  건설 후 3~6턴 정도 짧게 유지

blocked_this_turn:
  매 턴 decide 시작 시 초기화
```

만료 누락 시 발생 가능한 버그:

```text
1. recovery가 영구 지속되어 정상 경제를 계속 우회
2. 특정 지역이 영구 차단되어 확장 불능
3. sacrifice가 영구 유지되어 중요한 기지를 계속 버림
4. blocked_this_turn이 남아 다음 턴 정상 후보까지 차단
```

## 권장 구현 방식

### 1단계: 계측만 추가

가장 먼저 실제 행동을 바꾸지 않고 판단값을 로그로만 찍는다.

추천 필드:

```text
mode
recovery_reason
base_candidate
base_kind
recent_loss
enemy_eta
my_eta
can_garrison
expected_hold
block_reason
```

예시:

```text
# BOTDBG T42 mode=normal base=57 kind=ECON recent_loss=1 enemy_eta=2 my_eta=5 hold=1 block=frontier_recent_loss
# BOTDBG T14 mode=recovery base=12 kind=DEF recent_loss=1 enemy_eta=6 my_eta=1 hold=5 allow=safe_recovery
```

### 2단계: recovery 최소 행동만 적용

행동 변경은 다음으로 제한한다.

```text
조건:
  rush_guard == false
  recovery_until >= turn
  생존 base <= 1
  강제 수비 처리 후 a.train_n == 0
  !war

행동:
  후방 안전 기지 후보가 있으면 건설/파견
  없고 병력 부족이면 최소 훈련
```

### 3단계: base_plan_health는 후보 감점부터 적용

즉시 차단보다 후보 점수 감점이 안전하다.

```text
recent_loss && !DEF && !SAC:
  score -= large_penalty
```

차단은 매우 좁게:

```text
recent_loss
AND enemy_eta <= my_eta
AND expected_hold < 3
AND dist_to_opp_hq < dist_to_my_hq
```

### 4단계: SAC_BASE는 방어 금지까지 같이 구현

SAC_BASE를 도입한다면 반드시 다음도 같이 해야 한다.

```text
sacrifice_regions[region] = turn + short_ttl
방어 draft/pin에서 sacrifice region 제외
gather/rally가 SAC_BASE를 핵심 stage로 잡지 않게 감점
```

SAC_BASE를 지었는데 방어 로직이 지키러 가면 최악이다.

## 적용 전 체크리스트

아래 질문에 답할 수 없으면 구현하지 않는 편이 안전하다.

```text
1. recovery가 켜진 정확한 이유를 로그로 설명할 수 있는가?
2. recovery가 econ_open을 직접 true로 만들지 않는가?
3. rush_guard 활성 중 recovery가 일반 로직을 열지 않는가?
4. base 후보를 ECON/DEF/SAC로 분류한 근거가 로그에 남는가?
5. 최근 파괴구역이라도 방어/미끼 목적이면 왜 허용했는지 설명 가능한가?
6. 건설 차단 시 used/budget/mission이 그대로 일관적인가?
7. center/true_center는 일반 base 차단에 휘말리지 않는가?
8. SAC_BASE라면 방어 병력이 투입되지 않는가?
9. base 차단으로 남은 골드가 의도치 않게 HQ 업그레이드로 새지 않는가?
10. 기존 승리 로그에서 전방 발사점이 사라져 공격이 늦어지지 않는가?
```

## 결론

`rushguard recovery`는 적용 가치가 높고 충돌 위험도 비교적 낮다. 단, `rush_done`과 `econ_open`을 직접 건드리면 안 되고, rushguard 해제 이후 제한된 회복 행동으로만 구현해야 한다.

`base_plan_health`는 승률을 올릴 가능성이 있지만 충돌 위험이 더 크다. 단순 차단 로직이 아니라 `ECON_BASE`, `DEF_BASE`, `SAC_BASE` 목적 분류가 필요하다. 특히 최근 파괴구역을 무조건 막으면 방어 기지, 중앙 재건, 전방 발사점 확보 같은 기존 승리 패턴을 깨뜨릴 수 있다.

가장 안전한 적용 순서는 다음이다.

```text
1. 두 로직 모두 BOTDBG 계측만 추가
2. rushguard recovery 최소 행동 적용
3. base_plan_health는 후보 감점으로 시작
4. 충분한 로그 검증 후 좁은 조건에서만 차단
5. SAC_BASE를 도입한다면 방어 금지 로직까지 함께 적용
```

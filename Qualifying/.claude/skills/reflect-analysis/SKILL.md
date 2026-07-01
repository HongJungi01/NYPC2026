---
name: reflect-analysis
description: NEXT NATION 경기 분석 문서(패배·승리·취약·손해오더 findings)를 봇 스냅샷에 반영. 반영 전 각 finding을 실제 봇 코드와 대조해 (A)의도된 기능 정상 (B)없는 기능 우연 (C)의도된 기능 모호구현 패인 (D)무의미 행동 으로 분류하고 시간지평(N턴 뒤 결과/턴수 오산/200턴 내 끝낼 수 있다는 오판)까지 진단한 뒤, 메인 패배요인 극복용 기능추가·구현수정을 새 스냅샷에 적용하고 정확성 게이트(SIM/OBS)까지 실행 후 정지. "분석 반영", "스냅샷에 적용", "reflect analysis" 요청 시 사용. analyze-logs와 독립.
---

# /reflect-analysis — 분석을 봇 스냅샷에 반영 (분류 게이트)

경기 **분석 문서**를 읽어, 각 finding이 봇 기능상 무엇인지 **실제 코드와 대조해 분류**한 뒤,
메인 패배요인을 극복하는 수정을 **새 스냅샷**에 적용하고 **정확성 게이트까지** 돌린 후 **정지**한다.
A/B·채택은 사람이 별도 결정한다.

> **독립 스킬**: `/analyze-logs`와 별개다. 입력은 **소스 무관한 분석 md**면 무엇이든 된다
> (`Logs/LlmAnalyzer/llmanalyzed/`의 자동분석이든, 사람이 쓴 `Logs/Reflected/*.md`든, 지정 경로든).
> 같은 basename의 facts(`_facts/<id>.facts.md`)·raw 로그가 있으면 근거 보강에 쓰되, 없어도 동작한다.
> 운영 루프·부기는 [[../../../Logs/POLICY.md]](반영 루프)·`submission/POLICY.md`(스냅 규칙)를 따른다.

---

## 1. 입력 & HEAD 판정
- `/reflect-analysis <경로 또는 id ...>` — 분석 md 경로를 직접 받거나, id면 `Logs/LlmAnalyzer/llmanalyzed/`
  → `Logs/Reflected/` 순으로 찾는다. 무인자면 `llmanalyzed/`의 미반영 후보를 나열하고 어느 걸 묶을지 묻는다.
- 각 분석 md를 읽고, 있으면 facts/ raw 로그도 읽어 수치 근거를 확인한다.
- **HEAD = 최신 타임스탬프 스냅샷**: `submission/` 에서 `^[0-9]{8}\.cpp$` 중 가장 큰 값
  (현재 `07012033.cpp`). ⚠️ `Logs/LEDGER.md`·`REGRESSION.md` 헤더의 HEAD 표기(07011816)는 **뒤처져
  있으니 신뢰하지 말고 파일 기준으로 판정**하고, 부기 시 갱신한다. `hybrid.cpp`·과거 스냅은 편집 금지.

## 2. findings 추출 & 메인 패배요인 선정
각 분석에서 (턴/맥락 · 행동 · 효과 · 승/패 기여)를 뽑는다. 여러 분석을 배치로 받으면 **빈도·영향으로
메인 패배요인을 선정**한다(예: 지속 순손해 소모전, 지킬 수 없는 과확장, 실패/과소 공성, 무방비 본진,
승기 회군, 200턴 성장차, 헛발동 경제희생). 승리 경기의 취약 국면·상대 결정적 블런더도 참고한다.
이 스킬의 목표는 **그 메인 패배요인을 극복하는 기능추가·개선**이다(적극적으로).

## 3. 분류 (코어) — 각 finding을 봇 코드와 대조
각 finding을 `submission/<HEAD>.cpp`의 담당 기능에 매핑(§5 코드지도)하고 아래로 분류한다.
**단정 전 반드시 해당 코드를 Grep/Read로 확인**한다(라인 번호는 시작점일 뿐, 편집으로 이동함).

| 클래스 | 판정(코드 대조) | 반영 방향 |
|---|---|---|
| **A 의도·정상** | 담당 게이트가 있고 조건대로 발동, 결과는 설계 귀결 | 승리요인이면 유지/강화; 패인이면 **설계 재고**(신중, A/B 필수) |
| **B 부재·우연** | 그 행동을 내는 코드가 **없음**(창발/운) | 이득이면 **기능화**(의도적으로); 손해면 **없는 가드 신규 구현** |
| **C 의도·모호구현** | 게이트는 있으나 임계/조건/시간창이 모호·오차 → 패인 | **임계/조건/ETA 산식 수정**(대개 시간지평 문제) |
| **D 무의미** | 기능이 발동했으나 효과 0(낭비) | **효과 조건 게이트 추가** 또는 제거 |

**시간지평 진단(모든 finding에 병기)** — 사용자 요구:
- **(i) 몇 턴 뒤 미래에 결과가 나는 결정인가** → 결정 시점과 효과 시점의 지연. lookahead/ETA 정합성
  문제인지(예: 지금 좋아 보여 저지른 게 N턴 뒤 손해).
- **(ii) 턴수 계산을 틀렸나** → 거리/ETA/grind 산식 오차(도착·요격 타이밍 오판).
- **(iii) 200턴/요격창 내에 끝낼 수 있다고 판단한 게 틀렸나** → can_finish/winning_finish/deadline 버퍼가
  낙관적이었는지(실제론 못 끝냄 → 병력 소모/타임리밋 패).
→ 이 진단이 수정 위치를 시간지평 게이트(§5)로 좁혀 준다.

**REGRESSION 대조(필수)**: `Logs/REGRESSION.md` #1~#14와 대조해 (a) 기존 가드를 되돌리는 반영이
아닌지, (b) **되돌린 패턴(#14 blunt income 게이트) 재도입**이 아닌지 점검. 재도입 후보면 반영하지 말고
**플래그**(surgical 대안 + A/B 필수라고 안내).

## 4. 반영안 결정 (메인 패인 극복, 적극적)
분류별로 **구체 수정**을 정한다: 어느 매니저/게이트, 어떤 임계/조건/산식을, 무엇을 왜 어떻게.
- B(부재) → 없는 기능/가드를 전략층에 **신규 구현**. C(모호) → 조건·임계·시간창 **수정**.
  D(무의미) → 효과 있을 때만 발동하도록 **게이트**. A(패인) → 설계 재고(신중).
- **반영하지 않는 finding은 이유를 명시**(과적합/스냅 한정 현상, 되돌림, 의도된 정상, 근거 부족 등).
- 여러 findings를 **하나의 새 스냅샷**에 묶어 반영한다(메인 패인 위주). 변경마다 메커니즘 근거를 남긴다.

## 5. 코드 반영 지도 (현재 HEAD 07012033 기준; 편집 전 Grep로 재확인)
**결정계층**
- `StrategyManager`(K-day lookahead) ~L1573/L1588 — posture 선택, `K=min(MAX_TURN-turn,16)`.
- `presets`(BALANCED/TURTLE/RUSH/EXPAND) ~L671 — attack_mass·late_game·attack_turn·deep_hq·army_extra.
- `gen_cmds` 서브매니저: Economy ~L809 / Movement·수비 ~L858 / Military·공격 ~L1093 / Production ~L1399.
- `eval_state` ~L623 — HQ손실 ±1e9, HQ HP 60, damage 15, army 4, 근접압박 22, 지역지배 8, income 1, gold 0.05.

**시간지평 게이트(시간 관련 패인은 여기)**
- endgame `(MAX_TURN-turn)<=20` ~L1137 · deadline_push `(MAX_TURN-turn)<=eta+15` ~L1295 · standoff_push ~L1298.
- lane/horizon/ETA ~L874 · conscription 창 `life=eta+grind` ~L1032 · trainable_def `min(10,(turns+1)*cap)` ~L1057.
- can_finish_hq `finish_force>=hq_clear`(무버퍼) ~L1139 · buffered `+min(12,eta*opp_cap)` ~L1146 ·
  winning_finish(적HQ±인접 ≥ resolver) ~L967 · safe_mass 4:1/5:1 ~L1109/L1113 · IDLE_GOLD decay ~L1478.
- 공격 게이트: anti-trickle ~L1316 · growth-ahead `income 우위 아니면 원거리 HQ 금지` ~L1340 ·
  home-reserve hold-back(전방 스테이징 ≥4면 홈수비) ~L1350.

**기능 게이트 ↔ REGRESSION(#=재도입 금지·회귀 점검 체크리스트)**
- #1 회군가드(winning_finish는 recall 제외) · #2 위협 중 잉여공격/끝내기 진행 · #3 no-idle 골드전환 ·
  #4 끝내기 불가 시 최근접 거점 4:1 · #5 러쉬 인지 parity 생산 · #6 clear 가능 시 5:1 안 기다리고 HQ ·
  #7 건물<3면 조기 병력 차단 · #8 income_dom HQ L3 선업글 · #9 기지 레벨업은 병력 후 · #10 수비 징집
  (노동병 먼저·먼 공격수 회군X) · #11 dominant일 때만 채굴병 마무리 · #12 기지 열세=behind 리테이크 ·
  #13 둘 다 열세면 HQ 업글 금지 · **#14 income 게이트=되돌림(재도입 금지)**. 각 항의 코드 위치는
  `Logs/REGRESSION.md` 표 참조.

## 6. 적용 → 새 스냅샷
1. HEAD를 **현재 시각 타임스탬프**로 복사: `submission/<MMDDHHmm>.cpp` (예: `07021530.cpp`).
   (PowerShell: `Copy-Item submission\07012033.cpp submission\<new>.cpp`.)
2. 새 스냅샷에 §4 수정 적용(원본 HEAD·과거 스냅은 건드리지 않는다).
3. 빌드(PowerShell, msys64):
   ```
   $env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH
   g++ -O2 -std=c++17 submission\<new>.cpp -o submission\<new>.exe
   ```
   컴파일 실패 시 고쳐서 통과시킨다.

## 7. 정확성 게이트 (필수, PASS까지)
기준 로그 `Logs\Test\raw\07011137\07010130_seed5.txt` 로 SIM/OBS 빌드·실행(PowerShell):
```
g++ -O2 -std=c++17 -DSIM_TEST submission\<new>.cpp -o <scratch>\new_sim.exe
g++ -O2 -std=c++17 -DOBS_TEST submission\<new>.cpp -o <scratch>\new_obs.exe
& <scratch>\new_sim.exe "Logs\Test\raw\07011137\07010130_seed5.txt"
& <scratch>\new_obs.exe "Logs\Test\raw\07011137\07010130_seed5.txt"
```
- **PASS**: SIM = `OK ... : N turns matched` + `ALL OK` (exit 0); OBS = `OBS OK ... observable state EXACT`
  + `ALL OK` (exit 0). (opp-gold 오차 수치는 추정치라 게이트 대상 아님 — 무시.)
- **FAIL**: `MISMATCH at TURN ...` / `OBS MISMATCH ...` → 시뮬 불변 위반. 수정해서 PASS시킨다(스냅 불변
  원칙상 같은 타임스탬프 파일을 계속 편집). 정확성 게이트를 통과 못 하면 반영을 중단하고 사람에게 보고.
- 여러 로그로 넓히려면 `Logs/Test/raw/**` 중 몇 개를 더 돌린다(선택).

## 8. 부기 (반영 확정 시, Logs/POLICY.md §7·§8)
- `Logs/LEDGER.md`: 반영 행 갱신/추가(id·종류·반영 변경 요약·스냅명·날짜). 헤더/문서의 stale HEAD 갱신.
- `Logs/REGRESSION.md`: 반영한 패인을 1줄 distilled로 추가(코드 위치 포함) + **스윕 카운터 n/5 증가**
  (5 도달 시 스윕 실행·기록·0 리셋). #14 등 되돌림은 재도입 안 했음을 확인.
- reflected 분석 파일 이동: `Logs/LlmAnalyzer/llmanalyzed/<id>.{md,txt}`(+`_facts/`) 또는
  `Logs/Reflected/`에 있던 걸 **`Logs/Reflected/`로 정리**(생애주기 통합, id 충돌 주의).

## 9. 정지 & 출력
아래를 출력하고 **정지**한다(자동 A/B·채택·롤백 없음):
- **분류표**: `finding → 클래스(A/B/C/D) → 시간지평(i/ii/iii) → 결정(반영/제외+이유) → 코드사이트`.
- 새 스냅명 + **정확성 게이트 결과**(SIM/OBS PASS) + 각 수정의 **메커니즘 근거**.
- **반영 제외 목록**과 이유.
- **A/B 권고**: 전략 변경이면 사람이 돌릴 명령 안내 —
  `powershell -File submission/abtest.ps1 -Cand submission/<new>.cpp -Base submission/<HEAD>.cpp -Seeds 20`
  (풀=8자리 스냅샷+spar; 채택 룰=뚜렷한 net 개선+단일상대 비회귀+메커니즘 근거). 되돌림 재도입 후보가
  있었다면 경고. 최종 채택/롤백은 **사람 판단**.

## 10. 안전장치 / 비목표
- **게이트까지만**: 코드 채택/롤백/커밋은 하지 않는다. A/B는 사용자 승인 시에만(자발 실행 금지) —
  근거: 정확성 게이트 통과해도 A/B에서 큰 전략 회귀가 난 사례(07011635·07011727) 있음.
- **되돌린 패턴 재도입 금지**(#14 blunt income 게이트). 필요 시 surgical 대안만 제안 + A/B 경고.
- **불변 스냅**: `hybrid.cpp`·과거 8자리 스냅은 편집하지 않는다. 새 스냅만.
- **독립성**: analyze-logs 파이프라인 내부에 의존하지 않는다(입력 분석 md만 소비, facts는 선택).
- 근거가 애매하면 반영하지 말고 플래그(거짓 반영 > 놓침). 봇 내부기능명 단정 금지(오버피팅 캐버트).

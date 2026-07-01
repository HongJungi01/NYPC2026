# 회귀 체크리스트 (REGRESSION)

반영 완료된 패인을 distilled 형태로 누적한다. 주기적 회귀 스윕은 **원본 로그 전체 대신 이 표**를
훑어, 각 패인이 현재 hybrid.cpp에서 여전히 방지되는지 확인한다. 규칙은 [POLICY.md](POLICY.md) §8 참조.

**스윕 카운터** — 마지막 스윕: 2026-07-01 (#1~#14) · 스윕 이후 반영: **0/5**
> 점검 기준 코드 = 현재 봇 = 최신 스냅샷 `submission/<MMDDHHmm>.cpp` (현재 **07011816** = 07011255 + 가드형 total_mob).
> 패인을 1건 반영할 때마다 n을 1 올린다. 5에 도달하면 스윕을 실행하고 0으로 리셋한다.

> ⚠️ **2026-07-01 자가대전 A/B 회귀 되돌림:** 스냅 07011635(위협탐지 재작성)·07011727(42602 income
> 게이트/피니시)이 정확성 게이트는 통과했으나 A/B에서 큰 회귀(07011255가 H2H 8-0, 풀 44-27 vs 13-58,
> spar 32-0 스윕)로 드러나 **07011255로 되돌림**(=07011816). #11은 07011255 원형 복귀, #14(income
> 게이트)는 **되돌림**(재도입 말 것 — A/B net -34). 그 두 리워크 방향은 재시도 전 반드시 abtest A/B.

| # | 패인 요약 | 출처 로그 | hybrid.cpp 수정 위치/커밋 | 스윕 점검 포인트 |
|---|-----------|-----------|---------------------------|------------------|
| 1 | 승기에서 회군: 적 HQ 코앞 병력을 본진 수성하려 전군 회군 → 이긴 판을 던짐 (T85) | 69845_1P | `gen_cmds` total-mob recall에 `winning_finish` 가드 — 적 HQ(인접 포함) lethal 병력은 recall 제외 | `winning_finish=true`일 때 recall이 `opp_hq`/인접 forward 유닛을 빼지 않는지 |
| 2 | 위협 시 백도어 / 수비·공격 병행: 위협이면 잉여까지 멈춰 과수동(먼 HQ 올인만 위험). 방어병 예약 후 잉여는 계속 공격해야 | 69845_1P · 40039 Bot4/5 | C1: `if (hq_threatened && !winning_finish && !can_finish_hq && target==opp_hq) launch=false` | 위협 중에도 거점 치기·끝내기는 진행하고 먼 HQ 올인만 보류되는지 |
| 3 | 유휴 골드: 안전한데 골드를 쌓아두고 4턴간 아무것도 안 함(빈 커맨드 블록) → 물자 사장 (T73-76) | 37881 Bot5_1P | `gen_cmds` MilitaryManager no-idle 폴백 — `!hq_threatened && budget>TRAIN_COST+600` 면 paced cap 넘겨 병력 생산(예비 600) | 안전+골드 600 초과 시 빈 커맨드 없이 병력/업글로 전환되는지 |
| 4 | 거점 미타격·산만: 항상 HQ만 노려 물자를 압박으로 못 바꾸고 산개 (T186 HQ 함락) | 37881 Bot5_1P | C2: 끝낼 수 없으면(`!can_finish_hq`) 가장 가까운 적 거점, `safe_mass`=max(attack_mass, resolver, 4·(def+turret)) | 끝내기 불가 시 최근접 거점을 4:1로 치는지 |
| 5 | 러쉬 무대응(빌드업 포함): 페이싱이 생산을 지연·뱅킹 → 러쉬 도착 시 병력 부족 (Bot3 T76) | 38335 Bot3 · 40039 Bot3 | H: `opp_adv>=4`면 `needed_now=parity`. C3: `behind=opp_army>army`면 `needed_now` 하한 `min(opp_army,MAX)-2`, `econ_open`에 `behind` 추가 | 진격 전 빌드업(behind)에도 병력 수를 ~2 이내로 추격하는지 · 러쉬 시 즉시 parity인지 |
| 6 | 못 끝냄: 5:1 과보수로 이길 판을 안 끝내고 거점 감 → 턴리밋 패 (Bot4) | 38335 Bot4 · 40039 Bot4 | C2: `can_finish_hq = attack_force >= min_mass_to_take(HQ 수비대+터렛)`(resolver). 참이면 `target=opp_hq, need_mass=resolver` | 클리어 가능하면 5:1 안 기다리고 HQ로 커밋하는지 |
| 7 | 조기 병력 과투자: HQ+거점2개 전에 병력부터 뽑아 경제·확장 지연 | 38335_1P Bot4 | `gen_cmds` I-a: `econ_open = my_b.size()>=3 || hq_threatened || rush || behind` 아니면 정기 train·no-idle 차단 | 건물<3·비위협·비러쉬·비behind면 정기 생산이 멈추고 확장에 투자되는지 |
| 8 | 타이브레이크 대비 미흡: 경제 지배해도 HQ 업글 안 해 200턴 HP 타이브레이크 패 | 40039 Bot5 · 37881 | C4: `income_dom`(수입 20:13 & 골드+600)이면 `want`에 `(income_dom && level<3)` | 경제 지배 시 HQ가 L3까지 선제 업글되는지 |
| 9 | 업글이 병력보다 앞섬: 기지 레벨업에 골드 써서 병력 생산 밀림 (T80) | 69845_1P | C5: 기지 레벨업을 Economy에서 빼서 훈련·surplus→HQ 뒤로, `army>=cap_army && !hq_threatened`일 때만 | 기지 레벨업이 병력(cap_army) 충족 후에만 일어나는지 |
| 10 | 수비 실패: 러쉬에 노동병을 안 데려오고(먼 공격수 회군) 기지에 1명씩 남겨 HQ 무방비 함락 | 38335·40039 Bot3 | G(snap 07011238): 방어 징집 창 `life=eta+grind`(시즈 동안 합류 가능)·HQ는 노동병 먼저 징집(기지 비움)·total-mob은 `life` 안에서만(먼 공격수 회군X)·위협 중 fill-labor 정지·`behind`면 먼 HQ 올인 보류 | 러쉬 시 노동병이 기지 비우고 HQ로 오는지 · 먼 공격수를 무용 회군 안 하는지 · 위협 중 기지 리필 안 하는지 |
| 11 | 못 끝냄(무승부): 압도적 우세인데 전방 스테이징만 하고 본진 안 침(대군이 채굴 중) → DRAW | 73710_2P | J1(snap 07011255, 현 HEAD 07011816에 유지): `dominant`(병력2배&≥8)이면 `finish_force=attack_force+laborers`, `dominant&&can_finish_hq&&launch`면 채굴병까지 동원. ⚠️ dominant 게이트 필수. (07011727의 endgame 정제는 A/B 회귀로 되돌림) | 압도 시에만 채굴병 동원되는지 · 비압도 초반엔 안 터지는지 |
| 12 | 기지 열세 방치: 상대 기지↑ 장기 골드 밀림인데 대응 안 함 | 74194_2P | J2(snap 07011255): `behind`에 `wv.opp_b.size()>my_b.size()` 추가(attack G4 · Military C3 둘 다) → 생산 개방·리테이크 | 기지 열세면 생산 열리고 F가 적 기지 치는지 |
| 13 | 열세 시 업글 악수: 상대 기지·병력 둘 다↑인데 업글(600원=병력5)해서 수비 붕괴 | 74194_2P | J3(snap 07011255): `deep_behind=opp기지>내기지 && opp병력>내병력`이면 Economy HQ `want` 무효화 | 둘 다 열세면 HQ 업글 안 하고 병력 가는지 |
| 14 | ~~무의미한 소모전으로 성장 말림 (income 게이트)~~ **되돌림** | 42602_4·5 | ⚠️ J4 income 게이트(07011727)는 **A/B에서 큰 회귀**(255 기준 30-80 vs 67-32, net -34; spar에도 패)로 **되돌림**. 42602의 소모전 진단 자체는 유효하나 income 게이트 구현은 강봇을 수동적으로 만들어 손해 > 이득. **미해결로 남김**(255의 J1 과투입 잔존). 재시도 시 blunt income 게이트 말고 "산술상 못 이기는 원거리 올인"만 집는 surgical 수정 + abtest 필수 | (해당 로직 HEAD에 없음) |

# 회귀 체크리스트 (REGRESSION)

반영 완료된 패인을 distilled 형태로 누적한다. 주기적 회귀 스윕은 **원본 로그 전체 대신 이 표**를
훑어, 각 패인이 현재 hybrid.cpp에서 여전히 방지되는지 확인한다. 규칙은 [POLICY.md](POLICY.md) §8 참조.

**스윕 카운터** — 마지막 스윕: (없음) · 스윕 이후 반영: **2/5**
> 패인을 1건 반영할 때마다 n을 1 올린다. 5에 도달하면 스윕을 실행하고 0으로 리셋한다.

| # | 패인 요약 | 출처 로그 | hybrid.cpp 수정 위치/커밋 | 스윕 점검 포인트 |
|---|-----------|-----------|---------------------------|------------------|
| 1 | 승기에서 회군: 적 HQ 코앞 병력을 본진 수성하려 전군 회군 → 이긴 판을 던짐 (T85) | 69845_1P | `gen_cmds` total-mob recall에 `winning_finish` 가드 — 적 HQ(인접 포함) lethal 병력은 recall 제외 | `winning_finish=true`일 때 recall이 `opp_hq`/인접 forward 유닛을 빼지 않는지 |
| 2 | 위협 시 백도어: HQ가 못 막을 파도에 직면했는데 sub-critical 병력을 적 HQ로 흘림 (T74) | 69845_1P | `gen_cmds` launch 게이트 — `hq_threatened && !winning_finish → launch=false` | HQ 피습 중 `winning_finish` 아니면 신규 공격을 안 흘리고 전방 거점에 묶이는지 |

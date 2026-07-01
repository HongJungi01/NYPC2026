# 사람이 읽기 쉬운 경기 요약

> 이 파일은 CSV/JSON을 직접 열지 않고도 경기 흐름을 판단할 수 있도록 자동으로 요약한 리포트입니다.
> 원본에 가까운 자세한 데이터는 `raw_data/` 폴더에 따로 보관됩니다.

## 한 줄 결론

- 이 경기는 무승부입니다. 단순히 비긴 경기가 아니라, B side가 `UNCONVERTED_ADVANTAGE` 문제로 승리 전환 기회를 놓친 경기로 분석합니다.
- 신뢰도는 `0.72`이며, 결정적 분기 후보는 `193-203`입니다.
- 자동 해석: 무승부지만 B side가 승리로 전환할 여지가 있었습니다. 놓친 핵심 요인은 UNCONVERTED_ADVANTAGE입니다. 근거: B side가 병력/건물/골드 우위를 승리 조건으로 전환하지 못함.

## 경기 기본 정보

- 결과: `RESULT DRAW TURN_LIMIT`
- 종료 턴: `200`
- 검증 상태: `VALID`
- WA 종료 여부: `False`
- 파싱 경고 수: `0`
- 맵 구역 수 N: `93`
- 핵심 거점 수 K: `15`
- 결과 유형: `DRAW`
- 승리 진영: `unknown`
- 패배 진영: `unknown`
- 무승부 시 승리 기회를 놓친 후보 진영: `B`

## 최종 격차

| 지표 | A side | B side | 차이(A-B) |
|---|---:|---:|---:|
| 생존 전사 수 | 12 | 33 | -21 |
| 건물 수 | 9 | 8 | +1 |
| 추정 골드 잔액 | 24260 | 20940 | +3320 |
| 총 훈련 수 | 53 | 78 | -25 |
| 총 손실 수 | 44 | 48 | -4 |
| 받은 피해량 | 184 | 235 | -51 |
| 공성량 | 0 | 6 | -6 |

### 최종 격차 해석

- 생존 전사: B side가 21명 앞섭니다.
- 건물: A side가 1개 앞섭니다.
- 추정 골드 잔액: A side가 3320골드 앞섭니다.
- 총 훈련 수: B side가 25명 앞섭니다.
- 총 전사 손실: B side가 4명 앞섭니다.
- 받은 피해량: B side가 51피해 앞섭니다.
- 공성량: B side가 6피해 앞섭니다.

## 구간별 흐름

| 구간 | 턴 범위 | A 생존 | B 생존 | A 건물 | B 건물 | A 추정 골드 | B 추정 골드 | 해석 |
|---|---|---:|---:|---:|---:|---:|---:|---|
| 1구간 | 1-50 | 8 | 9 | 8 | 6 | 2930 | 2780 | B 병력 우위(-1명), A 건물 우위(+2개), A 골드 우위(+150) |
| 2구간 | 51-100 | 22 | 16 | 9 | 8 | 7620 | 7540 | A 병력 우위(+6명), A 건물 우위(+1개), A 골드 우위(+80) |
| 3구간 | 101-150 | 14 | 23 | 9 | 8 | 15160 | 12620 | B 병력 우위(-9명), A 건물 우위(+1개), A 골드 우위(+2540) |
| 4구간 | 151-200 | 12 | 33 | 9 | 8 | 24260 | 20940 | B 병력 우위(-21명), A 건물 우위(+1개), A 골드 우위(+3320) |

## 주요 타이밍

| 항목 | A side | B side |
|---|---:|---:|
| 첫 훈련 | 21 | 1 |
| 첫 이동 | 1 | 1 |
| 첫 업그레이드 | 3 | 3 |
| 첫 전투 피해 | 45 | 45 |
| 첫 공성 | - | 69 |

### 타이밍 해석

- 첫 훈련: B side가 20턴 빠릅니다. (A 21턴, B 1턴)
- 첫 업그레이드: 양쪽 모두 3턴으로 같습니다.
- 첫 전투 피해: 양쪽 모두 45턴으로 같습니다.
- 첫 공성: B side만 69턴에 기록되었습니다.

## 명령과 실제 이벤트 규모

| 항목 | A side | B side | 차이(A-B) |
|---|---:|---:|---:|
| MOVE 명령 | 243 | 169 | +74 |
| TRAIN 명령 | 53 | 78 | -25 |
| UPGRADE 명령 | 10 | 21 | -11 |
| MOVE 이벤트 | 958 | 969 | -11 |
| TRAIN 이벤트 | 53 | 78 | -25 |
| UPGRADE 이벤트 | 10 | 21 | -11 |
| DAMAGE 이벤트 | 108 | 104 | +4 |
| SIEGE 이벤트 | 0 | 2 | -2 |

## 피해가 컸던 턴

### 전사 피해 기준

- 1위: 124턴 / B side / 16 피해
- 2위: 142턴 / B side / 15 피해
- 3위: 100턴 / B side / 14 피해
- 4위: 102턴 / B side / 14 피해
- 5위: 125턴 / B side / 14 피해
- 6위: 126턴 / B side / 14 피해
- 7위: 141턴 / B side / 14 피해
- 8위: 103턴 / A side / 13 피해

### 공성 피해 기준

- 1위: 69턴 / B side / 3 공성
- 2위: 70턴 / B side / 3 공성

## 전투/공성 집중 구역

### 전사 피해가 많이 발생한 구역

- 1위: 0번 구역 / 누적 222
- 2위: 8번 구역 / 누적 52
- 3위: 92번 구역 / 누적 50
- 4위: 65번 구역 / 누적 41
- 5위: 35번 구역 / 누적 23
- 6위: 44번 구역 / 누적 17
- 7위: 46번 구역 / 누적 6
- 8위: 48번 구역 / 누적 6
- 9위: 52번 구역 / 누적 2

### 공성이 많이 발생한 구역

- 1위: 65번 구역 / 누적 6

## 해석

- 메인 요인: `UNCONVERTED_ADVANTAGE`
- 이슈 코드: `UNCONVERTED_ADVANTAGE`
- 근거: B side가 병력/건물/골드 우위를 승리 조건으로 전환하지 못함
- 사람이 확인할 필요: `False`
- 가장 중요한 분기 후보 턴: `198`

## 무승부 분석: 승리할 수 있었지만 놓친 지점

- 승리 전환 후보 진영: `B`
- 승리로 연결되지 못한 핵심 이유: `UNCONVERTED_ADVANTAGE`
- 설명: 전략적 우위가 있었지만 전선 집중이나 본부 압박이 부족했습니다.
- 승리 기회를 놓친 후보 턴: `198`
- 주요 근거: B side가 병력/건물/골드 우위를 승리 조건으로 전환하지 못함

### 왜 승리로 연결되지 못했는가

- 최종 생존 전사 B:33 / A:12
- 최종 건물 수 B:8 / A:9
- 첫 공성 B:69 / A:없음
- 이벤트 수 B MOVE:969, TRAIN:78, SIEGE:2

### 어떤 판단이었다면 승리로 전환할 수 있었나

- 우위가 생긴 시점에 병력을 한 방향으로 묶어 전선 돌파를 시도합니다.
- 피해가 큰 병목 구역을 피하거나, 숫자 우위가 있을 때만 진입합니다.
- 마지막 25% 턴 구간에서는 골드 축적보다 공격 생산과 공성 이동을 우선합니다.

### 사람이 확인할 턴

- `raw_data/commands.csv`에서 승리 기회를 놓친 후보 턴 전후 10턴의 MOVE/TRAIN/UPGRADE 명령을 확인하세요.
- `charts/unit_paths.png`에서 공격 병력이 본부 방향으로 모였는지, 중간 거점에서 멈췄는지 확인하세요.
- `charts/siege_by_turn.png`에서 공성량이 늦게 나오거나 끊긴 구간을 확인하세요.

## 자동 진단 상세

- 분석 관점: `무승부를 승리 기회를 놓친 경기로 분석`
- 승리 전환 후보 진영: `B`
- 메인 요인: `UNCONVERTED_ADVANTAGE`
- 신뢰도: `0.72`
- 우선순위: `high`
- 점검 대상 모듈: `strategy_decision`
- 추천 액션: 병력/경제 우위가 생긴 시점에 확장보다 본부 압박을 우선하세요.
- 사람이 재검토해야 하는가: `False`

### 진단 근거

- B side가 병력/건물/골드 우위를 승리 조건으로 전환하지 못함

### 분석 가정

- 골드와 HP는 로그 이벤트 기반 추정값입니다.

### 치명적 분기점 후보

- 198턴: `UNCONVERTED_ADVANTAGE` (신뢰도 0.72)

## 빌드업 행동 타임라인

- 1턴 / A side / `MOVE`: A side MOVE A2 2 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 1턴 / A side / `MOVE`: A side MOVE A3 6 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 1턴 / B side / `MOVE`: B side MOVE B2 90 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 1턴 / B side / `TRAIN`: B side TRAIN 1 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 3턴 / A side / `UPGRADE`: A side UPGRADE 2 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 3턴 / B side / `UPGRADE`: B side UPGRADE 90 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 4턴 / B side / `MOVE`: B side MOVE B3 86 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 4턴 / B side / `MOVE`: B side MOVE B4 90 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 8턴 / A side / `UPGRADE`: A side UPGRADE 6 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 14턴 / B side / `UPGRADE`: B side UPGRADE 86 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 15턴 / B side / `MOVE`: B side MOVE B4 74 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 21턴 / A side / `TRAIN`: A side TRAIN 1 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 22턴 / A side / `MOVE`: A side MOVE A4 18 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 22턴 / B side / `UPGRADE`: B side UPGRADE 74 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 24턴 / A side / `TRAIN`: A side TRAIN 1 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 25턴 / A side / `MOVE`: A side MOVE A5 27 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 25턴 / A side / `UPGRADE`: A side UPGRADE 18 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 25턴 / B side / `TRAIN`: B side TRAIN 1 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 26턴 / B side / `MOVE`: B side MOVE B5 65 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.62)
- 29턴 / A side / `UPGRADE`: A side UPGRADE 27 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.48)
- 198턴 / B side / `CRITICAL_TURN`: 병력/건물 격차가 크게 벌어진 후보 턴 (연결 요인: `UNCONVERTED_ADVANTAGE`, 신뢰도 0.72)

## 무승부 원인 후보

- `UNCONVERTED_ADVANTAGE`: 전략적 우위가 있었지만 전선 집중이나 본부 압박이 부족했습니다. (신뢰도 0.72)

## 먼저 열어볼 차트

1. `charts/alive_warriors_by_turn.png` - 병력 격차가 언제 벌어졌는지 확인
2. `charts/gold_balance_by_turn.png` - 추정 골드 흐름 확인
3. `charts/building_count_by_turn.png` - 건물/거점 격차 확인
4. `charts/damage_by_turn.png` - 피해가 집중된 턴 확인
5. `charts/unit_paths.png` - 병력 이동 경로 확인

- 차트 생성 상태: `raw_data/chart_generation_status.json`에서 확인

## 폴더 구조

- `human_summary.md`: 사람이 먼저 읽을 유일한 메인 리포트
- `raw_data/`: 사람이 직접 판단하기 힘든 모든 원자료와 보조 파일 보관
- `charts/`: PNG 그래프
- `raw_data/analysis_report.md`: 기존 자동 분석 리포트 백업
- `raw_data/single_game_diagnosis.md`: 기존 단일 진단 파일 백업
- `raw_data/strategy_recommendations.md`: 기존 전략 추천 파일 백업

## 전략 추천 통합

- 병력/경제 우위가 생긴 시점에 확장보다 본부 압박을 우선하세요.
- 점검 대상 모듈: `strategy_decision`
- 우선순위: `high`
- 대안 후보: 우위가 생긴 시점에 병력을 한 방향으로 묶어 전선 돌파를 시도합니다.
- 대안 후보: 피해가 큰 병목 구역을 피하거나, 숫자 우위가 있을 때만 진입합니다.
- 대안 후보: 마지막 25% 턴 구간에서는 골드 축적보다 공격 생산과 공성 이동을 우선합니다.
- `charts/alive_warriors_by_turn.png`에서 병력 격차가 벌어지는 시점을 먼저 확인하세요.
- 분기점 전후 10턴의 명령은 `raw_data/commands.csv`에서 확인할 수 있습니다.
- 실제 피해가 집중된 구역은 `raw_data/combat_zones.csv`와 `charts/map_combat_zones.png`를 같이 보세요.

## 주의

- 골드와 HP는 로그에 직접 기록되지 않아 추정값입니다.
- 이 요약은 자동 진단 결과이므로, 결정적 턴 주변의 원본 명령과 차트를 함께 확인하는 것이 좋습니다.

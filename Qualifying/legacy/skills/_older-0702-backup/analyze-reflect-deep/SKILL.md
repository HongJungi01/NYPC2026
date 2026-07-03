---
name: analyze-reflect-deep
description: NEXT NATION 일체형 루프(스냅샷 대조 심층 분석 기반) — /analyze-logs-deep로 raw 로그를 코드 대조 DEEP 분석 md로 만들고(대조 코드는 호출 시 첨부·지정, 기본 HEAD), 이어서 미반영 분석 md들을 /reflect-analysis 절차(예비분류 재검증 모드)로 새 봇 스냅샷에 반영, 정확성 게이트(SIM/OBS)까지 수행 후 정지. input/이 비어 있으면 llmanalyzed/의 미반영 분석을 바로 반영. 새 스냅샷 이름은 <MMDDHHmm>d.cpp(deep분석 반영 표식). "딥 분석하고 반영까지", "심층 분석 반영", "deep analyze and reflect" 요청 시 사용.
---

# /analyze-reflect-deep — 심층(DEEP) 분석 → 반영 일체형

`/analyze-logs-deep`(1단계)와 `/reflect-analysis`(2단계)를 **중간 확인 없이 시퀀스로**
수행하는 얇은 오케스트레이터. 절차 정본은 각 스킬의 SKILL.md — 여기에는 **델타만** 기술한다.
`/analyze-reflect`(단순 일체형)와의 차이 = 1단계가 DEEP(코드 대조)이고, 2단계 분류가
재검증 모드이며, 스냅 접미가 `d`.

## 인자 & 대상 수집 (0단계)
`llmanalyzed/` 에 md가 남아 있음 = **분석 완료·미반영** (반영되면 `Logs/Reflected/` 로
이동하므로). 이를 전제로 대상을 수집한다:
- 인자 없음 → `input/*.txt` 전부(**DEEP 분석 필요**) + `llmanalyzed/*.md` 전부(**바로 반영**).
- `<id ...>` → 각 id를 분류: `input/<id>.txt` 있으면 분석 필요, 없고 `llmanalyzed/<id>.md`
  있으면 바로 반영, 둘 다 없으면 그 id는 알리고 건너뜀.
- **대조 코드 지정**(1단계에 그대로 전달, analyze-logs-deep 인자 규칙과 동일):
  `snap:<MMDDHHmm[a-z]?>`(스냅 id) / `snap:<경로.cpp>`(임의 경로) / 메시지에 첨부·언급된
  코드 파일. 없으면 HEAD.
- 케이스 정리: input에만 있음 → DEEP 분석 후 반영 / llmanalyzed에만 있음 → **분석 생략,
  바로 반영** / 둘 다 있음 → input 분석 후 **미반영 md 전부(새것+기존)를 한 배치로** 반영 /
  둘 다 비면 → 알리고 종료.

## 절차

1. **1단계 — DEEP 분석** (분석 필요 대상이 있을 때만; 없으면 스킵하고 바로 2단계):
   `.claude/skills/analyze-logs-deep/SKILL.md` 를 로드해 절차 전체를 수행한다
   (facts → 루브릭 md + 코드 대조 레이어·DEEP 헤더 마커·[코드 원인표] →
   `llmanalyzed/` 이동 → 요약).

2. **2단계 — 반영**: `.claude/skills/reflect-analysis/SKILL.md` 를 로드해 절차 전체를
   수행한다. 입력 = **1단계 산출 md + 0단계에서 수집한 기존 미반영 md, 한 배치**.
   기존 미반영 md가 단순(비-DEEP) 분석이면 **재분석하지 않고 그대로 소비**한다(멱등성) —
   재검증 모드는 DEEP md에만, 단순 md는 일반 분류. 스테이지 사이 사용자 확인 없음.
   - **§3 분류 = 재검증 모드**: DEEP md의 finding별 예비분류(A/B/C/D·시간지평·코드사이트)를
     처음부터 재도출하지 않고 **코드로 재검증**한다 — 코드사이트를 Grep/Read로 확인해
     맞으면 채택, 어긋나면 재분류(어긋난 사실도 분류표에 기록).
   - **새 스냅샷 이름 = `<MMDDHHmm>d.cpp`** (d = 스냅샷대조 deep 분석 반영 표식,
     `submission/POLICY.md` §1). 접미는 reflect 규칙을 따른다 — 배치에 DEEP md가 하나라도
     있으면 `d`, (폴백으로) 전부 단순 md면 `r`.
   - 모든 finding이 반영 제외로 판정되면 **스냅샷을 만들지 않고** 제외 사유만 보고한다.

3. **정지 & 출력** (reflect-analysis §9 형식 + 분석 요약):
   - 파일별 DEEP 분석 한줄요약(대조스냅·예비분류 분포 포함).
   - 분류표(finding → 예비분류 → 재검증 결과 → 반영/제외) · 새 스냅명(`…d`) ·
     SIM/OBS 게이트 결과 · 반영 제외 목록 · A/B 권고.

## 안전장치
- reflect-analysis §10 그대로: **게이트까지만** — A/B·채택·롤백·커밋은 사람.
  되돌린 패턴(#14) 재도입 금지. 과거 스냅·hybrid.cpp 불변.
- DEEP 분석이 반영 시점 HEAD와 다른 대조 코드를 썼다면(1단계 `snap:`/첨부 오버라이드 등)
  2단계는 **반영 대상 HEAD 코드로 다시 확인**한다 — 버전 간 코드가 이동했을 수 있다.
- 게이트 FAIL이 수정으로도 해소되지 않으면 반영 중단·보고 (분석 md는 남아 있으므로
  이후 `/reflect-analysis` 로 재시도 가능).

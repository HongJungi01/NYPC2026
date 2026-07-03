---
name: analyze-reflect
description: NEXT NATION 일체형 루프(단순 분석 기반) — /analyze-logs로 Logs/LlmAnalyzer/input/ raw 로그를 분석 md로 만들고, 이어서 미반영 분석 md들을 /reflect-analysis 절차로 새 봇 스냅샷에 반영, 정확성 게이트(SIM/OBS)까지 수행 후 정지. input/이 비어 있으면 llmanalyzed/의 미반영 분석을 바로 반영. 새 스냅샷 이름은 <MMDDHHmm>r.cpp(단순분석 반영 표식). "분석하고 반영까지", "일괄 분석 반영", "analyze and reflect" 요청 시 사용.
---

# /analyze-reflect — 단순 분석 → 반영 일체형

`/analyze-logs`(1단계)와 `/reflect-analysis`(2단계)를 **중간 확인 없이 시퀀스로** 수행하는
얇은 오케스트레이터. 절차 정본은 각 스킬의 SKILL.md — 여기에는 **델타만** 기술한다.

## 인자 & 대상 수집 (0단계)
`llmanalyzed/` 에 md가 남아 있음 = **분석 완료·미반영** (반영되면 `Logs/Reflected/` 로
이동하므로). 이를 전제로 대상을 수집한다:
- 인자 없음 → `input/*.txt` 전부(**분석 필요**) + `llmanalyzed/*.md` 전부(**바로 반영**).
- `<id ...>` → 각 id를 분류: `input/<id>.txt` 있으면 분석 필요, 없고 `llmanalyzed/<id>.md`
  있으면 바로 반영, 둘 다 없으면 그 id는 알리고 건너뜀.
- 케이스 정리: input에만 있음 → 분석 후 반영 / llmanalyzed에만 있음 → **분석 생략, 바로
  반영** / 둘 다 있음 → input 분석 후 **미반영 md 전부(새것+기존)를 한 배치로** 반영 /
  둘 다 비면 → 알리고 종료.

## 절차

1. **1단계 — 분석** (분석 필요 대상이 있을 때만; 없으면 스킵하고 바로 2단계):
   `.claude/skills/analyze-logs/SKILL.md` 를 로드해 절차 전체를 수행한다
   (facts → 루브릭 md → `llmanalyzed/` 이동 → 요약).

2. **2단계 — 반영**: `.claude/skills/reflect-analysis/SKILL.md` 를 로드해 절차 전체를
   수행한다. 입력 = **1단계 산출 md + 0단계에서 수집한 기존 미반영 md, 한 배치**.
   스테이지 사이 사용자 확인 없음.
   - **새 스냅샷 이름 = `<MMDDHHmm>r.cpp`** (r = 단순 로그분석 반영 표식,
     `submission/POLICY.md` §1). 단, 기존 미반영 md 중 DEEP 마커가 섞여 있으면
     reflect 접미 규칙에 따라 `d`가 된다(배치에 DEEP 하나라도 있으면 d).
   - 모든 finding이 반영 제외로 판정되면(과적합·되돌림·근거부족 등) **스냅샷을 만들지 않고**
     제외 사유만 보고한다.

3. **정지 & 출력** (reflect-analysis §9 형식 + 분석 요약):
   - 파일별 분석 한줄요약 (analyze-logs 요약 형식).
   - 분류표(finding → A/B/C/D → 시간지평 → 반영/제외) · 새 스냅명(`…r`) ·
     SIM/OBS 게이트 결과 · 반영 제외 목록 · A/B 권고.

## 안전장치
- reflect-analysis §10 그대로: **게이트까지만** — A/B·채택·롤백·커밋은 사람.
  되돌린 패턴(#14) 재도입 금지. 과거 스냅·hybrid.cpp 불변.
- 게이트 FAIL이 수정으로도 해소되지 않으면 반영 중단·보고 (분석 md는 남아 있으므로
  이후 `/reflect-analysis` 로 재시도 가능).

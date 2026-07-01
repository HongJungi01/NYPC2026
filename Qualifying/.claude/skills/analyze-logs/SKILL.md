---
name: analyze-logs
description: NEXT NATION 대전 raw 로그(승리·패배 모두)를 사람 스타일 승패요인 분석 md로 자동 변환. Logs/LlmAnalyzer/input/ 의 <id>.txt 각각에 대해 replay.py로 정확 facts(골드 역산·병력·유닛궤적·승패조건·오더품질 신호)를 뽑고, Logs/LlmAnalyzer/POLICY.md 루브릭으로 승패조건 분류·무의미/손해 오더 분류를 포함한 <id>.md 분석을 작성한 뒤 txt·md를 llmanalyzed/ 로 이동. "로그 분석해줘", "analyze logs", "승패요인 분석" 요청 시 사용.
---

# /analyze-logs — NEXT NATION 로그 자동 분석

`Logs/LlmAnalyzer/input/` 에 놓인 raw 로그를 사람 스타일 승패요인 분석으로 변환한다.
전체 정책·규칙·루브릭·출력형식은 **`Logs/LlmAnalyzer/POLICY.md` 가 정본**이다. 시작 전 반드시 읽는다.

## 인자
- 인자 없음 → `input/` 의 모든 `*.txt` 처리.
- `<id>` 지정(예: `/analyze-logs 5` 또는 `/analyze-logs 69845_1P`) → 해당 파일만 처리.

## 절차

0. **정책 로드**: `Logs/LlmAnalyzer/POLICY.md` 를 읽는다. 스타일 감을 위해 POLICY §4.3이 가리키는
   `Logs/Reflected/*.md` 예시 2~3개도 읽는다. (이미 이번 세션에서 읽었다면 생략 가능.)

1. **대상 수집**: `Logs/LlmAnalyzer/input/*.txt` 목록을 만든다(`.gitkeep` 제외). 인자로 `<id>` 가 오면
   그 파일만. 대상이 없으면 "input 비어 있음"을 알리고 종료.

2. **각 `<id>.txt` 에 대해 반복**:

   a. **facts 생성** (PowerShell 우선; 파이썬 표준 라이브러리만 필요):
      ```
      python Logs/LlmAnalyzer/tools/replay.py "Logs/LlmAnalyzer/input/<id>.txt" \
        --out "Logs/LlmAnalyzer/llmanalyzed/_facts/<id>.facts.md" \
        --json "Logs/LlmAnalyzer/llmanalyzed/_facts/<id>.facts.json"
      ```
      (id/파일명에 공백이 있으면 따옴표로 감싼다. 예: `"38335_1P Bot3"`.)
      종료코드 1 = HARD 오류(골드 과대추정) 있음 → 분석에서 그 게임 골드 해석을 주의하고 md에 플래그.

   b. **facts 읽기**: 방금 만든 `<id>.facts.md` 를 읽는다(정확 골드·병력·타이밍·본진압박·유닛궤적).
      필요하면 raw `input/<id>.txt` 원문도 교차 확인한다.

   c. **분석 작성**: POLICY §2(우리 진영=파일명 태그), §4 루브릭, §5 형식에 따라
      `Logs/LlmAnalyzer/llmanalyzed/<id>.md` 를 작성한다. 승리·패배 경기 모두 대칭 분석.
      - 우리 진영을 상단에 명시(태그 근거). facts와 어긋나면 `⚠️ 플래그`.
      - **승패 조건 분류**(§4.2): 전투 마무리(본부 파괴, 마무리 공격조) vs 200턴 본부 HP 판정
        (→ 성장차=기지/수입차 → 전투명령차로 귀인). facts `## 결과·승패 조건`, `## 성장 격차` 활용.
      - **무의미/손해 오더 분류**(§4.4): facts `## 오더 품질 신호`(실패 공성/순손해 교전/정크무빙/
        헛발동 후보)를 **보수적**으로 분류해 말미 `[무의미/손해 오더]` 섹션에. 애매하면 넣지 말 것.
        봇 내부기능명 단정 금지(오버피팅 캐버트) — "관찰상 순손해"로만.
      - 공격조는 **규모·출발지·출발/도착 턴**을 궤적표로 특정. 골드는 신뢰경계 이후 "≥"(하한)로만.
      - 사람 `Reflected/*.md` 톤의 턴 앵커 자유서술. 표 덤프 지양.

   d. **파일 이동**:
      - `input/<id>.txt` → `llmanalyzed/<id>.txt`
      - 작성한 md는 이미 `llmanalyzed/<id>.md` 에 있음(위치 그대로).
      - facts(.md/.json)는 이미 `llmanalyzed/_facts/` 에 있음.
      (PowerShell: `Move-Item -LiteralPath "...\input\<id>.txt" "...\llmanalyzed\<id>.txt"`.)

3. **요약 출력**: 처리한 파일별로 한 줄 —
   `<id>: <RESULT>(<승/패>) / 우리=<A|B> / 승패조건=<전투마무리|200턴HP> / 결정적 분기 t.. /
    <핵심 요인 요지> [손해오더 N건] [⚠️플래그/ HARD오류 있으면 표기]`.

## 주의
- 이 스킬은 **진단(md 생성)까지**만 한다. 봇 코드(`submission/*.cpp`) 는 수정하지 않는다
  (반영은 `Logs/POLICY.md` 루프에서 사람이 판단).
- raw `.txt` 는 실측 근거이므로 **내용을 수정하지 않는다**(이동만).
- 이미 `llmanalyzed/` 에 있는 id는 재처리하지 않는다(사람이 명시적으로 요청하지 않는 한).
- replay.py 가 실패하거나(파싱 경고 다수/HARD 오류) facts와 로그가 모순되면, 임의 추측하지 말고
  md에 `⚠️ 플래그` 로 남겨 사람 확인을 요청한다.

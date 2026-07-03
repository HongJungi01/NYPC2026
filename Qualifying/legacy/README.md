# legacy/ — 옛 워크플로 아카이브 (참고 전용, 동결)

2026-07-03, 새 봇을 **처음부터(clean-room) 다시 설계**하기로 하면서, 지금까지의
스킬·로그분석 파이프라인·워크플로 정책 문서를 이 폴더로 넘겨 **동결**했다.
여기 있는 것은 **읽고 참고만** 한다 — 스킬을 다시 만들 때의 출발 자료다.

> ⚠️ 스킬은 여기로 옮겨지면서 `.claude/skills/` 밖으로 나가 **비활성화**됐다.
> `/analyze-logs` 계열 자동 로드/로그 분석 루프는 더 이상 돌지 않는다. (의도된 것.)

## 무엇이 들어있나

### `skills/` — 옛 로그분석 스킬 5종 (구 `.claude/skills/`)
- `analyze-logs`, `analyze-logs-deep`, `reflect-analysis`, `analyze-reflect`, `analyze-reflect-deep`
- 각 폴더 `SKILL.md` 하나. NEXT NATION 대전 raw 로그 → 승패요인 분석 md → 봇 스냅샷 반영
  루프를 정의하던 스킬들. 새 스킬 설계 시 프롬프트/절차 참고본.
- `_older-0702-backup/` — 최상위 `skills/` 에 있던 **07-02 구 백업**(더 오래됨·작음). 손실 방지용 보존.

### `log-pipeline/` — 스킬이 쓰던 분석 도구·루브릭
- `POLICY.md` — 구 `Logs/LlmAnalyzer/POLICY.md`. replay.py facts + 승패조건 분류 + 오더품질
  루브릭(§6 DEEP, §7 사람힌트)으로 `<id>.txt` → `<id>.md` 를 만들던 분석 루브릭.
- `tools/replay.py` — 구 `Logs/LlmAnalyzer/tools/replay.py`. 로그에서 정확 facts(골드 역산·
  병력·유닛궤적·승패조건·오더신호) 추출기.

### `policies/` — 옛 워크플로 지배 문서 (파일명 접두로 원위치 구분)
- `submission_POLICY.md` — 구 `submission/POLICY.md`. 버전 스냅샷 명명·HEAD=제출·레거시 동결·
  crossplay(관찰) vs abtest(결정) 도트린.
- `Logs_POLICY.md` — 구 `Logs/POLICY.md`. 로그→분석→반영→정확성게이트 루프 정의.
- `LEDGER.md` — 구 `Logs/LEDGER.md`. 로그별 반영 상태 대장.
- `REGRESSION.md` — 구 `Logs/REGRESSION.md`. 패인 재발방지 체크리스트 + 스윕 카운터.

## 여기에 **없는**(살아있는) 것 — 어디로 갔나
- **봇 코드·배틀 하니스**: `submission/` 그대로. 스냅샷 `.cpp` 31종 + `main/variants/spar_base/
  hybrid/deepwork.cpp` + `abtest.ps1`·`crossplay.ps1`·`crossplay_par.ps1`·`buildpool.ps1`.
  용도 = 새 봇을 만들었을 때 **배틀 승률 검증 전용**. (동결·손대지 않음.)
- **실측 로그 데이터**: `Logs/` 그대로. `VersusPlayer/`·`VersusBot/`·`Reflected/`·`Test/` 와
  `Logs/LlmAnalyzer/{input,llmanalyzed}/`. 문서·도구만 여기로 왔고 데이터는 제자리.
- **게임 지식**: 루트의 `Rule.md`, `NEXT_NATION_승리전략_분석.md` — 재설계 참고, 제자리.
- **재설계 작업 공간**: `redesign/` — 기존 봇 기능·매니저 인벤토리와 앞으로의 클린룸 코드.

## 링크 주의
아카이브된 문서 안의 상대경로 링크(`[[..]]`, `hybrid.cpp#Lxxx` 등)는 **원위치 기준**으로
쓰였다. 이동 후 경로가 어긋날 수 있으나, 아카이브라 문서 내용은 원형 보존한다.

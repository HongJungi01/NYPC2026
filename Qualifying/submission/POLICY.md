# submission 운영 정책 (버전 스냅샷 + 교차대전 테스트)

봇 코드의 변경 이력을 **불변 스냅샷으로 누적**하고, 누적된 봇들끼리 **교차 대전으로
관찰만** 하는 체제. 봇 진화를 타임라인으로 보존하고, 다양한 상대 스파링 풀을 만든다.

## 1. 파일 규칙
- `<MMDDHHmm>.cpp` = 봇 버전 **스냅샷**(불변). 봇 코드를 의미 있게 바꿀 때마다 그 시각으로
  새 파일을 만든다. 예: 7월 1일 11:37 → `07011137.cpp`.
- **HEAD = 가장 최신 스냅샷** = 현재 봇 = **대회 제출 파일**. 다음 변경은 최신 스냅샷을 복사해
  새 스냅샷으로 이어간다(이전 스냅샷은 그대로 둔다).
- 베이스라인 스파링봇(`main.cpp`=mgr5, `test.cpp`=rush)은 **사용자가 제거**했다. 교차대전 풀은
  기본적으로 **누적 스냅샷들**이며, `submission/`에 베이스라인 `.cpp`가 있으면 자동 편입한다
  (harness가 존재하는 것만 상대로 씀 — 나중에 원하는 상대봇을 넣으면 그때부터 포함).
- `hybrid.cpp` = **동결(retired)**. 과거 작업 파일이며 최신 스냅샷의 조상. 편집하지 않는다.
- 빌드: `g++ -O2 -std=c++17 <name>.cpp -o <name>.exe` (PowerShell, 필요시
  `$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH`). `.exe`는 .gitignore(재생성 가능).

## 2. 교차대전 테스트 (`crossplay.ps1`)
- 새 스냅샷을 추가하면 실행: 그 스냅샷("New") vs **다른 모든 참가자**(이전 스냅샷들 + 있으면 베이스라인).
- 참가자당 **서로 다른 10개 시드 × 1판**(맵이 점대칭이라 좌우 스왑 불필요).
- referee: `Seongho/Game_NEXT_NATION_ver1/engine/testing-tool-practice.py`
  (`--seed S -l <log> -a <New.exe> -b <opp.exe>`). New는 항상 LEFT(-a).
- 실행: `powershell -File submission/crossplay.ps1 [-New <MMDDHHmm>]` (생략 시 최신 스냅샷).

## 3. 결과 로그
- 요약: `Logs/Test/<New>_crossplay.md` — 상대 × 시드 그리드(W/L/D) + W-L-D 집계. **커밋한다.**
- raw referee 로그: `Logs/Test/raw/<New>/` — 경기당 수천 줄이라 **.gitignore**(로컬 분석용).
  패인이 보이면 [[../Logs/POLICY.md]] 반영 루프로 가져갈 수 있다.

## 4. 자가대전 A/B 튜닝 (2026-07-01 도입 — 기존 "관찰 전용" 해제)
- **`crossplay.ps1`(교차대전)은 여전히 관찰 전용**: 결과로 롤백/무의미판정/채택거부 안 함.
- **`abtest.ps1`(A/B)로는 승률 튜닝을 한다**: 후보 vs 베이스라인을 **동일 풀·동일 시드** 페어로
  돌려 W-L-D로 채택/기각. 풀 = 8자리 스냅샷 **+ 고정포스처 spar 봇**(`spar_base.cpp`에서
  `-DSCRIPT_BOT -DSCRIPT_POLICY=presets::RUSH|TURTLE|EXPAND|BALANCED`). 오버피팅 완화의 핵심은
  **풀 다양화**(계보만 아니라 spar 포함)다.
- 채택 룰: **뚜렷한 net 개선 + 단일상대 비회귀 + 메커니즘 근거**. 미미/혼조는 베이스 유지.
  정확성 게이트(SIM/OBS)는 항상 선행. ⚠️ 풀에 **외부 봇이 없어** 외부상대 대응 변경의 *이득*은
  측정 불가 — 그런 변경은 메커니즘 근거로 판단.
- 근거: 정확성 게이트만으론 **전략 회귀를 못 잡는다**(2026-07-01 리워크 2건이 게이트 통과했으나
  A/B에서 큰 회귀로 드러나 되돌림). 방침 상세 = 메모리 `dev-testing-cadence`.

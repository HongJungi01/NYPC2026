# submission 운영 정책 (버전 스냅샷 + 교차대전 테스트)

봇 코드의 변경 이력을 **불변 스냅샷으로 누적**하고, 누적된 봇들끼리 **교차 대전으로
관찰만** 하는 체제. 봇 진화를 타임라인으로 보존하고, 다양한 상대 스파링 풀을 만든다.

## 1. 파일 규칙
- `<MMDDHHmm>.cpp` = 봇 버전 **스냅샷**(불변). 봇 코드를 의미 있게 바꿀 때마다 그 시각으로
  새 파일을 만든다. 예: 7월 1일 11:37 → `07011137.cpp`.
- **HEAD = 가장 최신 스냅샷** = 현재 봇 = **대회 제출 파일**. 다음 변경은 최신 스냅샷을 복사해
  새 스냅샷으로 이어간다(이전 스냅샷은 그대로 둔다).
- `main.cpp`(mgr5) / `test.cpp`(rush) = **고정 베이스라인** 스파링 상대. 건드리지 않는다.
- `hybrid.cpp` = **동결(retired)**. 과거 작업 파일이며 최신 스냅샷의 조상. 편집하지 않는다.
- 빌드: `g++ -O2 -std=c++17 <name>.cpp -o <name>.exe` (PowerShell, 필요시
  `$env:PATH = "C:\msys64\ucrt64\bin;" + $env:PATH`). `.exe`는 .gitignore(재생성 가능).

## 2. 교차대전 테스트 (`crossplay.ps1`)
- 새 스냅샷을 추가하면 실행: 그 스냅샷("New") vs **다른 모든 참가자**(이전 스냅샷들 + main + test).
- 참가자당 **서로 다른 10개 시드 × 1판**(맵이 점대칭이라 좌우 스왑 불필요).
- referee: `Seongho/Game_NEXT_NATION_ver1/engine/testing-tool-practice.py`
  (`--seed S -l <log> -a <New.exe> -b <opp.exe>`). New는 항상 LEFT(-a).
- 실행: `powershell -File submission/crossplay.ps1 [-New <MMDDHHmm>]` (생략 시 최신 스냅샷).

## 3. 결과 로그
- 요약: `Logs/Test/<New>_crossplay.md` — 상대 × 시드 그리드(W/L/D) + W-L-D 집계. **커밋한다.**
- raw referee 로그: `Logs/Test/raw/<New>/` — 경기당 수천 줄이라 **.gitignore**(로컬 분석용).
  패인이 보이면 [[../Logs/POLICY.md]] 반영 루프로 가져갈 수 있다.

## 4. ⚠️ 관찰 전용 규칙 (엄수)
- 교차대전 결과는 **기록만** 한다. 결과가 나쁘다고 **롤백하거나**, 변경을 "무의미"로
  판정하거나, 채택을 거부하는 등 **결과 기반 행동을 하지 않는다.**
- 이유: 다양·강한 상대 하니스가 아직 얕아 승률로 튜닝하면 오버피팅([[../Logs/REGRESSION.md]]
  회귀 트랩과 동일 위험). 지금은 스파링 풀을 **누적**하는 단계이며, 승패는 관찰 데이터일 뿐이다.

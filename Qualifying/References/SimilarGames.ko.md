# 유사 게임 & 도메인 횡단 AI 기법 — NEXT NATION 봇용 (한글 읽기판)

[StarCraft.ko.md](StarCraft.ko.md)의 짝 문서. 스타크래프트는 아이디어 소스의 하나일 뿐이고,
NEXT NATION([../Rule.md](../Rule.md))은 정확히 말하면 **그래프-영토, 동시제출, 결정론적,
완전정보, 턴당 ~100ms 예산** 게임이다. 이 성격은 스타크래프트보다 **프로그래밍 게임 대회**에
더 잘 맞고, 가장 바로 써먹을 수 있는 강력한 아이디어(그래프 ROI, 롤링 호라이즌 탐색, 영향력 맵,
배정 매칭)는 거기서 나온다. 이 문서는 그런 게임·봇·기법을 모으고, 각 항목에 알고리즘 요약 +
NEXT NATION 적용법을 붙였다.

> 이 파일은 사용자 읽기용 한글판이다. 정식(영문) 원본은 [SimilarGames.md](SimilarGames.md),
> 인덱스는 [README.md](README.md). 이 문서를 촉발한 도메인 횡단 분석: [GeminiReference.md](GeminiReference.md).

---

## 1. 왜 스타크래프트 너머를 보나

NEXT NATION의 성격과 각 성격이 주는 레버:

- **턴제 + ~100ms 예산** → 백그라운드 연산 불가; *예산 안에 결정*해야 함.
  → 실시간 반응 제어보다 **애니타임 휴리스틱 탐색(RHEA/빔)** 이 유리.
- **결정론 + 완전정보** → **정확 포워드 시뮬레이터** 제작 가능(StarCraft.ko.md §5 키스톤).
  → "후보 계획을 시뮬→점수→최선 선택"이 손룰보다 강함.
- **동시(블라인드) 제출 + 모든 유닛 동시 행동** → 분기 폭발; 고전 minimax/AB 부적합.
  → 유닛별 수 트리가 아니라 **매크로 행동 *시퀀스*** 를 진화/탐색(RHEA)하는 게 맞음.
- **구역 그래프 위 이동** → 거리·전선이 그래프 양. → 그래프 위 **영향력 맵**과 **시간 ROI**
  (이미 Floyd-Warshall `dist[][]` 보유).

**아래 모든 대회의 메타 교훈:** 결정론적 모델 + 턴 예산이 있으면 승리 공식은 *후보 계획 몇 개를
포워드 시뮬해서 최선을 고르는 것* — 실시간 반응(스타)도, 풀 게임트리 minimax(체스)도 아니다.

---

## 2. 가장 가까운 유사 게임 (+ 그 봇들)

### Planet Wars — Google AI Challenge 2010 *(수학적으로 가장 가까운 모델)*
행성(≈ 우리 구역/거점) 그래프; 각 행성은 생산력 보유; 함대를 간선 따라 보냄; 전투는 도착 수의
결정론적 뺄셈. NEXT NATION의 그래프 점령 + 결정론 전투 핵심과 거의 동일.
- [Gabor Melis — 우승 봇(코드)](https://github.com/melisgl/planet-wars) · [benzedrine 포스트모템](https://www.benzedrine.ch/planetwars.html) · [satirist Planet Wars 허브](http://www.satirist.org/ai/planetwars/)
- **NEXT NATION 적용:** 노드 점령의 시간 ROI(§3); 유휴 유닛을 전선으로 전진시키는 별도
  **보강(positioner) 패스**; 아군 중간 노드를 경유해 함대 라우팅(무료 이동 대응).

### Generals.io *(현존 가장 가까운 유사작)*
영토를 확장하고 매 턴 군대를 생산해 적 **장군**(= 본부)을 잡는 그리드 게임. 본부 파괴 승리 +
턴당 생산이 직결. (단, 전장 안개 있고 ~실시간; NEXT NATION은 완전정보 + 턴제.)
- [봇 서버/API](https://bot.generals.io/) · [EklipZ_ai(강봇)](https://github.com/EklipZgit/generals-bot) · [harrischristiansen/generals-bot](https://github.com/harrischristiansen/generals-bot) · [A3C 정책봇](https://yilundu.github.io/2017/09/05/A3C-and-Policy-Bot-on-Generals.io.html) · [RL 논문 2025](https://arxiv.org/html/2507.06825v2) · [전략 노트](https://gist.github.com/twolfson/4bbbb40bd8b7ed670694b5a4dae04931)
- **NEXT NATION 적용:** 태스크 기반 봇(확장/공격/방어 태스크를 위협도 등 지표로 점수화); 템포 —
  보강 틱 직전 점령; 초크 차단 & 군대 요격.

### microRTS (Ontañón) *(학술 탐색 기법의 보고)*
AI 연구용 최소 2인 제로섬 RTS 테스트베드 — 알고리즘을 깔끔히 비교하는 곳. 봇:
**PuppetSearch**(스크립트 단위 탐색), **NaïveMCTS / Naive Sampling**(조합적 멀티유닛 액션),
**StrategyTactics**(전략 탐색 + 전술 MCTS), **DRL 대회 우승자**.
- [microRTS 대회](https://sites.google.com/site/micrortsaicompetition/microrts) · [1회 대회 보고서(PDF)](https://scispace.com/pdf/the-first-microrts-artificial-intelligence-competition-34aoi2r58w.pdf) · [DRL 우승(2024)](https://arxiv.org/html/2402.08112v1)
- **NEXT NATION 적용:** 원시 수 조합이 아니라 **스크립트/추상 액션** 단위로 탐색(PuppetSearch);
  "모두 동시 이동" 액션 공간을 Naive Sampling으로 길들이기.

### Halite II / III (Two Sigma) *(동시제출 자원/영토)*
조선소에서 배 생산, 자원 채굴, 드롭오프 관리; 동시 이동; 최다 자원 승. **헝가리안(쿤-먼크레스)
매칭**으로 배→칸 최적 배정으로 유명.
- [Halite III 레포](https://github.com/HaliteChallenge/Halite-III) · [위키피디아](https://en.wikipedia.org/wiki/Halite_AI_Programming_Competition) · [포스트모템 봇 예시](https://github.com/dzou/halite3)
- **NEXT NATION 적용:** **배정 매칭**(§3)으로 전사→{노동 슬롯 / 확장 목표 / 공격 전선}; 유닛별
  그리디 대신 전역 최적으로 고가치 목표 우선.

### Kore 2022 (Kaggle) *(동시제출 다단계 "계획")*
조선소 건설, "kore" 채굴, **다단계 비행 계획**(문자열 프로그램)으로 함대 발사; 400턴까지 최다
kore(또는 상대 전멸) 승. 최상위 접근: 리더보드 강자 **모방학습**.
- [Kore 2022 대회](https://www.kaggle.com/competitions/kore-2022) · [모방학습 솔루션](https://github.com/khanhvu207/kore2022)
- **NEXT NATION 적용:** 액션을 단일 명령이 아니라 **여러 날짜 계획**(예 "A→기지 이동, 건설,
  노동")으로; 강한 로그로부터의 모방학습은 후속 트랙으로 유효.

### Lux AI — 시즌 1–3 (Kaggle / NeurIPS) *(대규모 동시제출 건설/유닛)*
두 플레이어가 자원 채집, 유닛/공장 건설, 확장; 동시; 많은 유닛 수. 베이스라인이 룰베이스·
브루트포스 탐색·RL·모방을 망라; 수십억 프레임 제공.
- [lux-ai.org](https://www.lux-ai.org/) · [Lux S2(NeurIPS'23)](https://github.com/Lux-AI-Challenge/Lux-Design-S2) · [Lux S3(NeurIPS'24)](https://github.com/Lux-AI-Challenge/Lux-Design-S3)
- **NEXT NATION 적용:** RL 전에 룰베이스/탐색 베이스라인이 경쟁력 있음을 확인; 멀티에이전트
  유닛 관리 패턴 참고.

### Battlecode (MIT) *(RTS 프로그래밍 대회)*
자율 군대 컨트롤러 작성; 결정론 엔진; 길찾기·분산 협조·턴당 고정 연산 예산 강조.
- [battlecode.org](https://battlecode.org/) · [2020 포스트모템](https://stonet2000.github.io/battlecode/2020/)
- **NEXT NATION 적용:** 예산 관리 규율 + 견고한 길찾기 패턴.

### 인접(우선순위 낮음 — 정보 모델 다름)
- **Diplomacy / CICERO** (Meta) — 동시 명령; 계획 + (여기선 무관) 협상: [ai.meta.com/research/cicero](https://ai.meta.com/research/cicero/).
- **Stratego / DeepNash** (DeepMind) — 불완전정보 model-free RL: [Science 2022](https://www.science.org/doi/10.1126/science.add4679).
- **Freeciv / Civilization** — 4X 턴제 매크로(경제/테크), 단 호라이즌이 더 김.

---

## 3. 도메인 횡단 기법 (핵심 페이로드)

각 항목: *알고리즘 2–4줄* + *NEXT NATION 적용*. 모두 StarCraft.ko.md §5 빌딩블록에 꽂힌다
(탐색 계열은 **정확 포워드 시뮬레이터**가 공통 키스톤).

### RHEA — Rolling Horizon Evolutionary Algorithm *(1순위 탐색 후보)*
*알고리즘:* 향후 **K턴 매크로 행동**을 유전자로 인코딩; 개체군 유지; 변이/교배;
**각 유전자를 포워드 시뮬레이터로 K까지 굴려** 끝 상태를 평가; 최선 유전자의 첫 행동을 실행;
다음 턴 **호라이즌 롤링**(직전 최선으로 시드). 멀티액션/연속/동시 게임에서 MCTS 능가.
- [RHEA for GVGP (ToG 2021, PDF)](http://www.diego-perez.net/papers/RHEAforGVGP_ToG21.pdf) · [arXiv](https://arxiv.org/pdf/2003.12331) · [Gaina 박사논문](https://rdgain.github.io/assets/pdf/papers/gaina2021phd.pdf)
- **NEXT NATION 적용:** 유전자 = 하루치 `(UPGRADE / TRAIN n / MOVE 목록)`를 K일; 평가 = 상태
  평가함수(StarCraft.ko.md §5); minimax를 깨는 "전사 동시 이동"을 깔끔히 처리.

### 빔 서치 + 시뮬레이티드 어닐링 *(CodinGame 대회 메타)*
*알고리즘:* **빔** = BFS인데 매 단계 평가 상위 K개 상태만 유지(깊이 ~60 × 폭 ~1000이 ms 안에
가능); **SA** = 계획 시퀀스를 섭동, 더 나쁜 수도 감소 확률로 수용해 지역최적 탈출. 둘 다 포워드
시뮬레이터를 감쌈.
- [Coders Strike Back 보고서](https://www.codingame.com/blog/coders-strike-back-contest-report/) · [SA 입문](https://www.codingame.com/learn/simulated-annealing) · [확률적 탐색 글](https://www.codingame.com/blog/stochastic-algorithm-smash-the-code/)
- **NEXT NATION 적용:** 하루치 매크로 계획에 대한 빔(동일 상태 중복 제거); 계획 공간이 울퉁불퉁할
  때 이동/건설/훈련 선택에 SA.

### 멀티유닛 MCTS
*알고리즘:* **Naive Sampling / NaïveMCTS** 는 결합 멀티유닛 액션을 분해해 트리 폭발 억제;
**PuppetSearch / 스크립트·클러스터 UCT** 는 원시 액션 대신 소수 **스크립트**(또는 유닛 클러스터)
단위 탐색.
- 위 microRTS 봇 · [스크립트·클러스터 UCT (Justesen 2014)](http://julian.togelius.com/Justesen2014Script.pdf)
- **NEXT NATION 적용:** 원시 명령 곱집합 대신 소수의 스크립트형 포스처/빌드 템플릿 메뉴를 탐색;
  의사결정 레이어에서 RHEA의 대안.

### 헝가리안 / 배정 매칭
*알고리즘:* 쿤-먼크레스가 에이전트-목표 간 최대가중 완전매칭을 O(n³)에 해결. *NEXT NATION
적용:* 가중치 = 전사 *w*가 목표 *t*를 수행하는 가치(노동슬롯 채우기 / 거점 점령 / 전선 보강);
유닛별 그리디 대신 매 턴 한 번 풀어 전역 최적 배정.

### 영향력 / 위협 맵
*알고리즘:* 각 유닛 전력을 거리 감쇠로 맵에 확산; 노드값 = Σ아군 − Σ적; **전선 = 높은(적×아군)**;
자기 영향력 최대화 방향으로 이동, 깊은 음수 노드 회피.
- [Game AI Pro 2 — Modular Tactical Influence Maps (Dave Mark)](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter30_Modular_Tactical_Influence_Maps.pdf) · [Spatial Reasoning for Strategic Decision Making (Kevin Dill)](http://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter31_Spatial_Reasoning_for_Strategic_Decision_Making.pdf)
- **NEXT NATION 적용:** Floyd-Warshall 거리로 구역별 영향력 계산; Movement 매니저는 `아군 − 적 ≥
  마진`인 곳에서만 교전(그래프판 안티-짤짤이), 계산된 전선에서 집결지 선택.

### 시간 / ROI 노드 가치 *(Planet Wars)*
*알고리즘:* 지금 노드 점령 가치 = `생산력 × (남은 턴) − 이동 비용`, 후보 목표끼리 비교.
*NEXT NATION 적용:* 기지 ROI = `15·work_cap·(200 − 도착일) − (300 건설 + 10·이동 + 미래 식비)`;
확장 목표 순위 및 "이 시점에 지을 가치가 있나?" 판단.

### 물류 / 안전마진 자원관리
*알고리즘:* 식비 대비 투영 현금흐름 ≥ 0 유지; 굶주림 틱 유발 명령 금지. *NEXT NATION 적용
([GeminiReference.md](GeminiReference.md)):* 후방 건물을 노동 포화로 유지해 전선 자금 조달;
저녁 식비가 골드를 초과하는(−1hp 굶주림 "자살") 계획은 하드 **필터**. 어떤 탐색이 고르든 통하는 안전망.

### 모방 / RL *(참고만 — 인프라 부담, 후속 트랙)*
강한 로그(Kore, Lux)나 셀프플레이 리그(StarCraft.ko.md의 AlphaStar; Generals A3C)에서 정책 학습.
*NEXT NATION 적용:* 모방·셀프플레이 대상이 될 강한 스크립트/탐색 봇이 생긴 뒤 가능; 첫 빌드는 아님.

---

## 4. 종합 — NEXT NATION 권장 방향

[GeminiReference.md](GeminiReference.md)의 하이브리드를 다듬은 것:

1. **정확 포워드 시뮬레이터**(StarCraft.ko.md §5) — 키스톤; 먼저 만들고 로그로 검증.
2. **상태 평가함수** — `(내HQ체력 − 적HQ체력)·BIG + 미래골드·w + (수적우위 구역수)·w`
   (Planet Wars 스타일), 종료 = HQ 파괴 / 200일 HP 판정.
3. **의사결정 레이어 = 100ms 예산 내 포워드심 휴리스틱 탐색** — K일 매크로 계획에 대한 **RHEA**
   (또는 빔); 포스처 메뉴엔 스크립트/퍼펫 탐색 선택적.
4. **공간 레이어** — 영향력 맵(전선·집결·안전/자살 구역) + 확장 순서용 시간 ROI + 전사→태스크
   헝가리안 배정.
5. **안전 필터** — 물류 룰이 굶주림/명백한 패전투 명령을 거부.
6. 얇은 **매니저 골격**(StarCraft.ko.md §4a)이 위를 조직 — 스타크래프트 매니저는 *틀*, 여기 탐색 +
   공간 기법이 *두뇌*.

이것이 "매니저 틀 + Planet-Wars/CodinGame식 빠른 포워드심 탐색" 접근으로, 결정론·그래프·턴예산
게임에 가장 잘 맞는다.

---

## 5. 다른 문서와의 관계
- [StarCraft.ko.md](StarCraft.ko.md) — 스타크래프트 특화 아키텍처 + 공유 빌딩블록(거기 §5).
- **이 문서** — 더 넓은 게임들: 유사 게임·봇·도메인 횡단 기법.
- [GeminiReference.md](GeminiReference.md) — 이 문서를 촉발한 도메인 횡단 종합.
- [README.md](README.md) — 인덱스 / 라이브러리 사용법.

# 스타크래프트 AI 레퍼런스 — NEXT NATION 봇용 (한글 읽기판)

검증된 스타크래프트 AI 설계를 가져와 **NEXT NATION** 봇을 만들기 위한 지식 베이스다.
하나의 아키텍처에 못박지 않고 **여러 아키텍처(§4)** 를 만들어 비교하며, 모두 같은 빌딩블록(§5)을
공유한다. 이 문서는 *사람이 훑어보기 좋게* + *바로 구현에 쓸 수 있게*(기법마다 알고리즘 요약 +
NEXT NATION 적용법) 작성했다.

> 이 파일은 사용자 읽기용 한글판이다. 정식(영문) 버전이자 Claude가 구현에 참조하는 원본은
> [StarCraft.md](StarCraft.md). 두 문서는 내용이 같으며, 변경 시 함께 갱신한다.
> 짝 문서: [SimilarGames.ko.md](SimilarGames.ko.md) (유사 게임 & 기법). 인덱스: [README.md](README.md).

게임 규칙: [../Rule.md](../Rule.md). 규칙 판정의 정답지(레퍼리):
[../Seongho/files_3/testing-tool-practice.py](../Seongho/files_3/testing-tool-practice.py).

---

## 1. 이 문서 사용법

- §2 — 이 스타크래프트 기법들이 왜 이 게임에 통하는지, 그리고 이 게임이 주는 결정적 이점 하나.
- §3 — 레퍼런스 라이브러리(링크 + 무엇인지 + NEXT NATION 용도).
- §4 — 만들어 비교할 후보 아키텍처들. 각각 필요한 레퍼런스를 명시.
- §5 — 모든 아키텍처가 공유하는 빌딩블록(이것들만 잘 만들면 됨).

아키텍처 *X* 를 구현할 때: §4의 해당 항목 → 거기 적힌 §5 블록들 → 세부는 §3 출처 순으로 본다.

---

## 2. 왜 스타크래프트 AI가 NEXT NATION에 통하나

NEXT NATION은 2인용 **턴제, 결정론적, 완전정보** RTS이며 **점대칭 구역 그래프**(N≤109,
≤200일, 거점 ≈ 확장 가능한 기지, 본부는 `0`번·`N-1`번) 위에서 진행된다. 명령은 **동시 제출
(블라인드)** 이고 하루는 정해진 순서로 처리된다.

**가장 큰 레버:** 전투가 결정론적이고 매 턴 전체 상태가 관측되므로(결과 블록이 양쪽의
업그레이드/훈련/이동/피해/공성을 모두 보고함) **정확한 포워드 시뮬레이터**를 만들 수 있다.
스타크래프트 진영은 끝까지 *근사* 전투 시뮬(SparCraft)밖에 못 가졌지만, 여기선 레퍼리 규칙을 1:1로
재현 가능 → 얕은 룩어헤드만으로도 신뢰도가 높다.

| 스타크래프트 AI 서브시스템 | NEXT NATION 대응 |
|---|---|
| 매니저 분해 (UAlbertaBot) | Strategy / Economy / Military / Movement 매니저 |
| 빌드오더 탐색 (BOSS; makespan/ROI) | 기지+업그레이드 사다리를 경제 목표로 스케줄링 |
| 전투 시뮬 (SparCraft; 포트폴리오 그리디 / UCTCD) | 구역별 수(數) 기반 전투의 *정확* 해결 |
| 지형 분석 (BWEM/BWTA: 리전·초크·기지) | 주어진 인접 그래프 + Floyd-Warshall (하니스에 이미 있음) |
| 전략 포트폴리오 + 밴딧 선택 | 상위 포스처 전환 (러시 / 확장 / 수비) |
| 딥RL (AlphaStar) | 참고만 — 학습 인프라는 범위 밖 |

스타크래프트와의 핵심 차이: 숨겨진 건 상대의 **금화**와 **이번 턴 제출**뿐, 나머지(유닛 위치/체력,
건물)는 다 안다. 따라서 상대 모델링은 전장 안개 추론이 아니라 *의도*의 미러/휴리스틱으로 충분하다.

---

## 3. 레퍼런스 라이브러리

### 메타 / 큐레이션 목록 (여기부터)
- [awesome-starcraftAI (SKTBrain)](https://github.com/SKTBrain/awesome-starcraftAI) — 봇·논문·프레임워크 큐레이션. *용도:* 출발점.
- [Awesome-Broodwar-Resources](https://github.com/TheEngineeringBay/Awesome-Broodwar-Resources) — 커뮤니티 자료·튜토리얼.
- [Dave Churchill — StarCraft AI 자료](https://davechurchill.ca/starcraft/resources/) — AIIDE 주최자의 허브(논문·코드·강연).
- [Jay Scott의 StarCraft AI 블로그](http://satirist.org/ai/starcraft/blog/) — Steamhammer 저자; 가장 실전적인 봇 제작 글(초크/리전, 길찾기, 전략선택).
- [Liquipedia 브루드워 봇](https://liquipedia.net/starcraft/Category:Bots) — 봇별 이력·강점.

### 대회 & 생태계
- [AIIDE StarCraft AI Competition](https://liquipedia.net/starcraft/Artificial_Intelligence_and_Interactive_Digital_Entertainment_StarCraft_AI_Competition) — 2010년부터(Buro/Churchill); 풀게임 봇. *용도:* 풀게임 매크로+마이크로의 기준선.
- [SSCAIT](https://sscaitournament.com/) — 상시 학생 래더/스트림.
- [BWAPI](https://github.com/bwapi/bwapi) — 모든 브루드워 봇이 올라타는 C++ API.

### 기반 프레임워크 (아키텍처 템플릿)
- [UAlbertaBot](https://github.com/davechurchill/ualbertabot) ([위키](https://github.com/davechurchill/ualbertabot/wiki)) — **매니저 분해의 표준**. *용도:* 매니저 분할(Strategy/Production/Combat/Scout/Worker/Building) 그대로 차용 → §4(a).
- [CommandCenter](https://github.com/davechurchill/commandcenter) — UAlbertaBot 구조를 포크하기 쉽게 정리(BW+SC2). *용도:* 미러링하기 가장 깔끔한 템플릿.
- [python-sc2 (BurnySc2)](https://github.com/BurnySc2/python-sc2) — 가독성 좋은 매크로 봇. *용도:* 경제/병력 관리 관용구.
- [PySC2 / SC2LE (DeepMind)](https://github.com/google-deepmind/pysc2) — RL 환경. *용도:* 참고만.

### 레퍼런스 봇 (오픈소스 강봇 — 매크로/병력 로직 학습)
- [PurpleWave](https://github.com/dgant/PurpleWave) (Scala; BASIL/SSCAIT/SAIL 1위) — 현역 최강 BW 봇; 풍부한 전략+스쿼드 시스템. *용도:* 매크로 의사결정의 상한선.
- [Stardust](https://github.com/bmnielsen/Stardust) (C++ 프로토스) / [Locutus](https://github.com/bmnielsen/Locutus) — 최근의 강한 C++ 봇.
- [Steamhammer](http://satirist.org/ai/starcraft/steamhammer/) (C++ 저그, UAlbertaBot 포크) — 위 블로그와 한 세트; 실제 코드베이스 학습용.
- [SAIDA](https://github.com/TeamSAIDA/SAIDA) (삼성 SDS; 2018 AIIDE 우승) — 룰베이스 + 일부 ML.

### 핵심 기법 논문
- [RTS 게임 AI 서베이 (Ontañón 외, 2013)](https://inria.hal.science/hal-00871001/en) — 분야 전체 지도: 과제·방법·대회 결과. **제일 먼저 읽기.**
- [Build Order Optimization / BOSS (Churchill & Buro, AIIDE 2011)](https://davechurchill.ca/publications/pdf/aiide11-bo.pdf) — §5 빌드오더 스케줄러의 근거.
- [Robust Continuous Build-Order Optimization (Churchill, CoG 2019)](https://ieee-cog.org/2019/papers/paper_85.pdf) — 변화에 대응한 재계획.
- [Portfolio Greedy Search (대규모 전투) (Churchill & Buro, 2013)](https://www.semanticscholar.org/paper/f78a522c99cd3056dbfbbeae9d3410f08ab12027) — §5 전투 리졸버의 근거.
- [Script- and cluster-based UCT (Justesen 외, 2014)](http://julian.togelius.com/Justesen2014Script.pdf) — 원시 액션이 아니라 스크립트 단위 탐색.
- [SparCraft 전투 시뮬](https://github.com/davechurchill/ualbertabot/wiki/SparCraft-Home) — (근사) BW 전투 시뮬레이터; 우리는 정확판으로.
- [Heuristic Search Techniques for RTS Games (Churchill 박사논문)](https://davechurchill.ca/publications/pdf/DavidChurchill_phd_thesis.pdf) — BOSS+전투탐색 통합; **가장 깊게 볼 한 편.**
- [부분관측 하 상위 전략 선택 (2018)](https://arxiv.org/abs/1811.08568) + [RTS용 조합 멀티암드 밴딧](https://arxiv.org/pdf/1710.04805) — §4(d) 포트폴리오/밴딧의 근거.
- [지형 분석: 초크 탐지 & 리전 분해 (Perkins)](https://ojs.aaai.org/index.php/AIIDE/article/view/12405) · [BWEM 라이브러리](https://bwem.sourceforge.net/) · [BWTA2](https://github.com/s7jones/bwta2) — 우리는 리전/거리를 하니스에서 이미 얻으므로 개념 참고용.

### 딥RL (참고만 — 인프라 범위 밖)
- [AlphaStar — SC2 그랜드마스터 (Nature 2019)](https://www.nature.com/articles/s41586-019-1724-z) ([블로그](https://deepmind.google/blog/alphastar-grandmaster-level-in-starcraft-ii-using-multi-agent-reinforcement-learning/)) — 멀티에이전트 리그 셀프플레이.
- [SC2 빌드오더 최적화: DRL + MCTS](https://arxiv.org/pdf/2006.10525).

---

## 4. 테스트할 후보 아키텍처

모두 §5를 재사용한다. 대략 쉬움→어려움 순.

**(a) 매니저 분해** — *아이디어:* Strategy/Economy/Military/Movement 매니저로 분할. Strategy가
포스처 + 빌드오더 *목표*를 정하면 하위 매니저들이 하나의 골드 예산을 두고 우선순위대로 그리디하게
명령을 채운다. *레퍼런스:* UAlbertaBot, CommandCenter, PurpleWave. *장점:* 명료·빠름·디버깅 쉬움.
*단점:* 룩어헤드 없이 반응형.

**(b) 정확 시뮬 탐색** — *아이디어:* 정확 포워드 모델을 활용. 매크로 = 경제/병력 목표를 향한
빌드오더 탐색(BOSS); 전술 = 정확 전투 리졸버 위에서 전투 탐색(포트폴리오 그리디 / UCTCD).
*레퍼런스:* BOSS 논문, Portfolio Greedy, Churchill 박사논문, Script-UCT. *장점:* 상한 가장 높음;
이 게임의 결정론을 정면으로 활용. *단점:* 더 복잡; 100ms/턴 예산 지켜야 함.

**(c) 하이브리드** *(자연스러운 타깃)* — *아이디어:* (a)의 매니저 골격을 쓰되, 핵심 의사결정 몇
개(공격/확장/테크 타이밍, 훈련 수)만 후보 계획 몇 개를 정확 시뮬레이터로 K일 전방 시뮬 +
평가함수로 골라낸다. *레퍼런스:* (a)+(b). *장점:* 실용적; 이득 나는 곳에만 룩어헤드. *단점:* 깔끔한
계획열거 + 평가 심(seam)이 필요.

**(d) 전략 포트폴리오 + 밴딧** — *아이디어:* 스크립트형 풀게임 전략 세트를 UCB/상대모델로 선택.
*레퍼런스:* 전략선택 + 조합밴딧 논문. *장점:* 단순한 메타 레이어. *단점:* 적합도 낮음 — 상대가
매판 다르고 단일 대회 런 안에서 판 간 기억이 없음; 판 간 학습기보다 *판 내 포스처 전환*으로 더 유용.

---

## 5. 공유 빌딩블록

한 번 잘 만들어 §4의 모든 아키텍처가 재사용. 모든 규칙 타이밍의 정답지는 레퍼리 `run_game` 순서:
`apply_upgrades → apply_moves → apply_train_charge → apply_day_movement → spawn_trained →
apply_day_combat → apply_day_siege → apply_evening_work → apply_evening_upkeep`.

**정확 포워드 시뮬레이터** — *무엇:* 위 순서 그대로 `(상태, 양측 명령) → 다음 상태`로 정확히
진행. *NEXT NATION:* 레퍼리 판정을 포팅 — 건설/이동/훈련 → 이동 단계 → 전투(집중사격: 체력
최소 → 번호 최소; 포탑은 공격 횟수 추가; 남는 공격은 건물 공성) → 노동 수입 `15·min(전사,work_cap)`
→ 보급 `2`/전사를 번호순, 못 내면 −1체력. 하니스 `read_turn_result`가 우리 쪽 대부분을 이미
구현했으니, 양측의 가상 명령으로 일반화하면 된다. **여기가 핵심** — 먼저 만들고 레퍼리 로그로
단위테스트할 것.

**빌드오더 / 경제 ROI 스케줄러** (BOSS) — *무엇:* 목표(예 "수입 ≥ X" 또는 "D일까지 전사 N명")가
주어지면 가장 빠른 행동 순서를 찾기. *알고리즘:* makespan 기준 DFS + 분기한정; 프레임 단위로 돌리지
말고 선행조건+골드가 충족되는 **다음 시점으로 점프**; 매크로 액션으로 분기수 억제; 나이브 계획으로
초기 상한 설정. *NEXT NATION:* 액션 = 기지건설(300, 빈 거점에 전사 필요) / HQ·기지 업글 / 훈련;
골드는 하루당 `+15·노동자 −2·전사`로 투영; "구역당 하루 1건설", HQ `train_cap` 제약 준수.

**전투 리졸버 / 포트폴리오** — *무엇:* 이 구역 싸움을 이기나, 그리고 얼마의 병력으로? *알고리즘:*
정확 리졸버로 집중사격 루프를 끝까지; 병력 배분은 구역별 투입 규모에 대한 포트폴리오 그리디
힐클라이밍 + 플레이아웃 평가. *NEXT NATION:* 판정이 정확하므로 "구역 R을 이기나?"는 (내 수, 적 수,
체력 분포, 포탑)의 결정함수 — 소모전에서 이기려면 `방어 + 포탑 + 마진` 초과로 투입(란체스터);
절대 찔끔 보내지 말 것.

**리전 / 지형 유틸** — *무엇:* 거리, 확장 순서, 전선, 점령 구역. *NEXT NATION:* 하니스
`calculate_paths`(Floyd-Warshall `dist[][]`/`nxt[][]`, "ceil-유클리드 합 최소, 동률→번호 작은 쪽"
규칙 경로) 재사용. 각 거점을 `dist(내HQ,·)` vs `dist(적HQ,·)`로 내것/경합/적것 분류; 확장순서 =
가까운 순; 대칭 미러맵 `r ↔ N-1-r`.

**평가함수** — *무엇:* 시뮬레이션된 상태에 점수를 매겨 탐색/하이브리드 레이어가 쓰게. *NEXT
NATION:* `w1·(내HQ체력 − 적HQ체력) + w2·경제(수입 또는 노동포화) + w3·(내가 수적우위인 구역 수)
+ w4·병력가치 − w5·과생산`. 종료: HQ 파괴 = ±∞; 200일에는 HP 판정 사용. 가중치가 튜닝 표면.

**상대 모델** — *무엇:* 룩어헤드를 위해 상대의 숨은 의도를 채우기. *NEXT NATION:* 전체 상태는
알고 금화·이번 턴 제출만 숨겨짐. 기본 모델 = 우리 정책의 점대칭 미러(맵이 대칭이라 미러가 강한
사전확률), 또는 싸구려 휴리스틱(위협받는 건물 방어, 가까운 곳 확장, 모아서 밀기). 적 금화는 관측된
비용/수입을 시뮬레이션해 추정.

---

## 6. 현황 & 다음

- 레퍼런스 라이브러리: 이 문서(+ 영문 [StarCraft.md](StarCraft.md)).
- 아키텍처: 아직 미구현(휴리스틱 v1 프로토타입은 삭제; §4를 §5 위에 새로 올림). 추천 순서: §5
  **정확 시뮬레이터** 먼저(키스톤) → §4(a)를 베이스라인으로 → §4(c) 하이브리드 탐색 얹기.
- 테스트: 기존 레퍼리 + `viewer.html`; 프로젝트 방침대로 변경마다 **한 판만** 스모크 테스트(반복
  배치 금지).

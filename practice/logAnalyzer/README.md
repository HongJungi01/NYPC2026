# logAnalyzer — 패배 로그 분석기 (engine-backed)

대전 로그를 **완전 복원**하고, **내 엔진**(submission/main.cpp 스냅샷)으로 매 수를 채점해
"왜 졌는지"를 객관적으로 진단한다. 마지막에 (옵션) **Gemini API** 로 한국어 패인 해설을 붙인다.

또한 **시간 사용 감사(time audit)** 로 "어느 구간에 시간을 더 쓰면 이득인지" 를 정량화한다:
내 총 사용시간/미사용 슬랙, 국면별 분포, 상대 시간배분 대조, 그리고 **시간 레버리지**(실제예산에선
손해인데 예산을 늘리면 손해0이 되는 수 = 시간을 더 쓰면 더 좋은 수를 두는 지점)를 표로 보여준다.

> 블런더 탐지·채점은 엔진만으로 **결정론적으로 완결**된다. LLM 은 해설만 담당(없어도 동작).

## 구성

```
logAnalyzer/
  *.txt                     # 입력: 대전 로그 (파일명 접두 1P/2P = 내 엔진이 맡은 플레이어)
  analyze.py                # 오케스트레이터: 파싱·리플레이·채점·리포트·Gemini
  engine/
    engine.hpp              # submission/main.cpp 엔진 스냅샷 (@ea901be, main() 제외)
    analyze_engine.cpp      # 루트 채점 CLI → JSON
    analyze_engine.exe      # 빌드 산출물
  reports/                  # 생성된 마크다운 리포트 (1P_27040.md ...)
  README.md
```

### ⚠ engine.hpp 동기화
`engine.hpp` 는 `submission/main.cpp`(FROZEN) 의 엔진 부분(Move/State/playMove/Solver)을
**그대로 복사한 스냅샷**이다. `main.cpp` 가 갱신되면 이 헤더도 재동기화해야 채점이 "현재 엔진" 과
일치한다. 현 스냅샷(@ea901be)은 증분 수생성(deriveMoves) + 자발적 패스 게이트(passGate) +
직전패스 비트(prevWasPass) 를 포함한다. `decide()` 는 3인자(prevWasPass)이며 분석용으로
완료깊이를 돌려주는 `int* lastDepth` 옵션 인자가 하나 더 있다(추가만, 동작 동일).

## 빌드

```sh
cd logAnalyzer/engine
# MSYS2 면 PATH 에 mingw64/bin 추가
export PATH="/c/msys64/mingw64/bin:$PATH"
g++ -O2 -std=c++17 analyze_engine.cpp -o analyze_engine.exe
```

## 실행

```sh
cd logAnalyzer
python analyze.py                          # 폴더 내 *.txt 전부 (기본 cap 4000/depth 18 → 정밀하지만 느림)
python analyze.py "1P#32399.txt" --no-llm  # 특정 로그, LLM 생략
python analyze.py "1P#32399.txt" --cap 2000 --depth 14           # 빠르게(얕게) 훑기
python analyze.py "1P#32399.txt" --budgets 800,1500,3000         # 시간예산 스윕 점 직접 지정
python analyze.py "1P#32399.txt" --no-sweep                      # 시간감사/스윕 끔(가장 빠름)
python analyze.py "1P#32399.txt" --with-pass                     # PASS 도 후보(주의: 아래)
```

> 시간 사용 감사가 핵심이라 **예산 스윕은 기본 동작**이다(미지정 시 `[800,1500,3000]` + 각 수의
> 실제예산·cap 을 합쳐 사용). 느리면 `--no-sweep` 로 끄거나 `--cap` 을 낮춰라.

| 옵션 | 기본 | 의미 |
|------|------|------|
| `--depth N` | 18 | 루트 반복 채점 깊이 상한 |
| `--cap MS` | 4000 | 포지션당 시간 상한(ms). **클수록 진단 정확**(특히 중반), 대신 느림. |
| `--budgets a,b,..` | `800,1500,3000` | `decide()` 시간예산 스윕(내 턴만). 각 수의 실제예산·cap 과 합쳐 "몇 ms면 최적수를 두나" 측정. |
| `--no-sweep` | off | 시간예산 스윕 비활성(빠름, 시간감사 레버리지 표가 비어짐) |
| `--with-pass` | off | 합법수 있어도 PASS 를 후보 포함. **상대 패스 미모델링 → 과대평가**(아래). |
| `--no-llm` | off | Gemini 해설 생략 |
| `--model` | `gemini-3.5-flash` | Gemini 모델(`env GEMINI_MODEL` 로도 지정) |

## 리포트 읽는 법

- **리플레이 정합성**: 재구성 점수 == 로그 SCORE 여야 함(파서/리플레이 자체검증). ❌면 결과 신뢰 불가.
- **cellLoss(손해 칸수)**: 그 포지션에서 **최선수 대비 몇 칸 손해**였나(0 = 최적). 평가 1칸 = 실제 1점.
- **수준%(qualityPct)**: 후보수 평가를 0~100 으로 정규화(높을수록 좋음).
- **순위 k/N**: 합법수 N 개 중 둔 수의 등수.
- **수렴깊이(convergenceDepth)**: 그 수가 "최적(동률 best)" 으로 평가되기 시작하는 탐색 깊이.
- **내 누적손해 vs 상대 누적손해**: 둘이 비슷한데 점수차가 크면 → **수 실수보다 구조적/초기 열세**.
- **시간압박**: 블런더인데 (실제예산 < 최적임계) 또는 저예산에서 발생 → 시간관리 문제 시사.

### 시간 사용 감사 읽는 법
- **미사용 슬랙**: `10000ms − 내 총 사용`. 크면(예: >1500ms) 예산 공식이 시간을 **과소 사용** 중.
- **국면별 분포 / 상대 최대값 대조**: 상대가 핵심 수에 시간을 몰아쓰는데(최대값 큼) 나는 평평하면,
  슬랙을 **핵심 구간에 재배분**할 여지가 있다는 신호.
- **시간 레버리지 표**: 실제예산에선 손해(>0)인데 현실적 예산(≤3000ms)으로 늘리면 손해0이 되는 수.
  - `깊이@예산` < `깊이@opt` 이면 "더 깊이 봤다면 더 좋은 수" — **깊이 기준이라 머신속도 무관**(1순위 지표).
  - `회수(칸)` 합 = 그 수들에 시간을 더 썼을 때 엔진 기준 회수 가능한 점수. 슬랙 안에 들면 **순이득**.
  - 표가 비어 있으면(더 줘도 같은 수) 패인은 시간배분이 아니라 **평가/탐색 구조** 쪽이다.
- ⚠ 시간 스윕의 ms/도달깊이는 **머신속도 의존**. `opt예산ms` 절대값보다 `깊이@예산 vs 수렴깊이` 를 우선 본다.

## 알려진 한계 / 해석 주의

1. **PASS 모델링**: 엔진의 `genMoves` 는 PASS 를 만들지 않는다(상대는 트리에서 패스 불가).
   따라서 `--with-pass` 의 PASS 가치는 **과대평가**된다. 기본은 PASS 제외(엔진 실제 선택지로 채점).
   다만 "모든 합법수가 똑같이 나쁘고 PASS 만 좋다" 는 결과는 **엔진이 패스를 못 둬서 생기는 구조적 약점**의
   신호일 수 있다(엔드게임 zugzwang).
2. **엔진 자기채점**: 채점도 같은 material-only 엔진이라, 엔진이 **원래 못 보는** 중반 수의 약점은
   채점에서도 안 보일 수 있다. → `--cap` 를 키워 더 깊이 보면(엔드게임은 정확히 풀리므로) 드러난다.
3. **시간 스윕**(`--budgets`)은 wall-clock 기반이라 **머신 속도 의존**. 깊이 기반 지표가 1순위.

## Gemini 해설 — 키/모델 설정

1) **키 발급**: <https://aistudio.google.com/apikey> 에서 API 키 생성.

2) **`.env` 작성** (커밋 제외됨):
```powershell
Copy-Item .env.example .env      # 그리고 .env 를 열어 키 붙여넣기
```
`.env` 내용:
```
GEMINI_API_KEY=발급받은_키
GEMINI_MODEL=gemini-3.5-flash
```

3) **확인**:
```sh
python analyze.py --check-llm    # 키 정상 여부 + 사용 가능한 모델 목록 출력
```
`--check-llm` 이 보여주는 목록에서 모델명을 골라 `.env` 의 `GEMINI_MODEL` 에 넣으면 된다.

4) **실행**: 이후엔 그냥
```sh
python analyze.py "1P#27040.txt"
```
키가 없거나 틀려도 크래시 없이 **결정론 리포트만** 생성하고 안내만 남긴다.

> 키는 환경변수로 직접 줘도 된다(`.env` 보다 우선): PowerShell `$env:GEMINI_API_KEY="..."`.
> `.env` 와 `reports/`, `engine/*.exe` 는 `.gitignore` 로 제외된다.

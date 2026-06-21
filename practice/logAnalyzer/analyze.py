#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
패배 로그 분석기 (engine-backed loss analyzer)

대전 로그를 완전 복원하고, 내 엔진(engine/analyze_engine.exe)으로 매 수를 채점한다.
  - 내 턴 : 둔 수의 quality%, 최선 대비 cellLoss(손해 칸수), 최적 수렴 깊이, (옵션)시간예산 스윕
  - 상대 턴: 상대가 둔 수를 내 엔진이 몇 % 로 평가하는지
결정론 마크다운 리포트를 만들고, (옵션) Gemini API 로 한국어 패인 해설을 덧붙인다.

사용:
  python analyze.py                         # logAnalyzer/*.txt 전부
  python analyze.py "1P#27040.txt"          # 특정 로그
  python analyze.py "1P#27040.txt" --no-llm
  python analyze.py "1P#27040.txt" --budgets 50,100,250,500,1000
옵션:
  --depth N   루트 채점 깊이 상한 (기본 18)
  --cap MS    포지션당 시간 상한(ms) (기본 4000). 작게 주면 빠르고 얕음.
  --budgets   decide() 시간예산 스윕(쉼표구분, 내 턴만). 미지정 시 생략(빠름).
  --no-llm    Gemini 해설 생략(블런더 분석은 LLM 없이 완결)
  --model     Gemini 모델(기본: env GEMINI_MODEL 또는 gemini-3.5-flash)
환경변수:
  GEMINI_API_KEY  (해설용. 없으면 자동으로 결정론 리포트만)
  GEMINI_MODEL    (모델 id override)
"""
import argparse
import glob
import json
import os
import subprocess
import sys

# Windows 한글 콘솔(cp949)에서 비ASCII 출력 시 크래시 방지 — 콘솔 출력을 UTF-8 로.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROWS, COLS = 10, 17
START_TIME_MS = 10000
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ENGINE_EXE = os.path.join(SCRIPT_DIR, "engine", "analyze_engine.exe")
REPORT_DIR = os.path.join(SCRIPT_DIR, "reports")
BLUNDER_CELL = 1.0  # cellLoss >= 1칸 이면 블런더로 집계
DEFAULT_MODEL = "gemini-3.5-flash"
# 시간감사 기본 예산 스윕(ms). 각 내 수마다 실제예산(budget_actual)과 cap을 합쳐 정렬·중복제거해 사용.
#   "이 수에 시간을 더 썼다면(=더 깊이 봤다면) 더 좋은 수를 뒀을까?" 를 측정.
DEFAULT_SWEEP = [800, 1500, 3000]
LEVERAGE_AFFORDABLE_MS = 3000  # opt_budget 이 이 이하면 "현실적으로 더 쓸 수 있었다"고 본다

# ---------------------------------------------------------------------------
# .env 자동 로딩 (logAnalyzer/.env 의 KEY=VALUE; 이미 설정된 환경변수는 유지)
# ---------------------------------------------------------------------------
def load_dotenv():
    p = os.path.join(SCRIPT_DIR, ".env")
    if not os.path.exists(p):
        return
    with open(p, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            os.environ.setdefault(k.strip(), v.strip().strip('"').strip("'"))

# 키 유효성 + generateContent 지원 모델 목록 확인
def check_llm(model):
    import urllib.request
    key = os.environ.get("GEMINI_API_KEY")
    if not key:
        print("GEMINI_API_KEY 미설정 — logAnalyzer/.env 에 넣거나 환경변수로 설정하라.")
        return
    url = f"https://generativelanguage.googleapis.com/v1beta/models?key={key}"
    try:
        with urllib.request.urlopen(url, timeout=30) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        print(f"키 확인 실패(키가 틀렸거나 네트워크 문제): {e}")
        return
    names = [m["name"].split("/")[-1] for m in data.get("models", [])
             if "generateContent" in m.get("supportedGenerationMethods", [])]
    print(f"✅ 키 정상. 현재 모델 설정: {model}" + ("" if model in names else "  ⚠ 목록에 없음 — 아래에서 고를 것"))
    print("generateContent 지원 모델:")
    for n in names:
        print(f"  {n}" + ("   <- 현재 설정" if n == model else ""))

# ---------------------------------------------------------------------------
# 파싱
# ---------------------------------------------------------------------------
def me_from_filename(path):
    base = os.path.basename(path).upper()
    if base.startswith("1P"):
        return 0  # FIRST
    if base.startswith("2P"):
        return 1  # SECOND
    raise ValueError(f"파일명 접두 1P/2P 로 내 엔진 플레이어를 알 수 없음: {path}")

def parse_log(path):
    """returns (init_rows[10], moves[(player,r1,c1,r2,c2,time)], score_first, score_second)"""
    init_rows, moves = None, []
    score_first = score_second = None
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            tok = raw.split()
            if not tok:
                continue
            # 혹시 모를 선행 라인번호 토큰 제거
            if tok[0].isdigit() and len(tok) > 1 and not tok[1].lstrip("-").isdigit():
                tok = tok[1:]
            head = tok[0]
            if head == "INIT":
                init_rows = tok[1:11]
            elif head in ("FIRST", "SECOND"):
                player = 0 if head == "FIRST" else 1
                r1, c1, r2, c2 = (int(tok[1]), int(tok[2]), int(tok[3]), int(tok[4]))
                t = int(tok[5]) if len(tok) > 5 else 0
                moves.append((player, r1, c1, r2, c2, t))
            elif head == "SCOREFIRST":
                score_first = int(tok[1])
            elif head == "SCORESECOND":
                score_second = int(tok[1])
            elif head in ("FINISH", "ABORT"):
                pass
    if init_rows is None or len(init_rows) != ROWS:
        raise ValueError(f"INIT 파싱 실패: {path}")
    return init_rows, moves, score_first, score_second

# ---------------------------------------------------------------------------
# 보드 리플레이 (engine.hpp playMove 의미 그대로)
# ---------------------------------------------------------------------------
def new_board(init_rows):
    num = [[int(init_rows[r][c]) for c in range(COLS)] for r in range(ROWS)]
    owner = [[-1] * COLS for _ in range(ROWS)]  # 0=FIRST, 1=SECOND, -1=없음
    return num, owner

def play_move(num, owner, counts, mv, mover):
    """mover: 0=FIRST, 1=SECOND. counts=[firstCnt, secondCnt]. 패스는 보드 불변."""
    r1, c1, r2, c2 = mv
    if r1 < 0:
        return
    for r in range(r1, r2 + 1):
        for c in range(c1, c2 + 1):
            if num[r][c] != 0:
                num[r][c] = 0
                owner[r][c] = mover
                counts[mover] += 1
            elif owner[r][c] != mover:
                oo = owner[r][c]
                owner[r][c] = mover
                counts[mover] += 1
                if oo != -1:
                    counts[oo] -= 1

def count_nz(num):
    return sum(1 for r in range(ROWS) for c in range(COLS) if num[r][c])

def count_legal(num):
    """현재 num 레이어의 유효 직사각형(합10 + 4변 버섯) 개수 = nLegal. engine genMoves 미러."""
    n = 0
    for r1 in range(ROWS):
        colsum = [0] * COLS
        colcnt = [0] * COLS
        for r2 in range(r1, ROWS):
            for c in range(COLS):
                v = num[r2][c]
                colsum[c] += v
                if v:
                    colcnt[c] += 1
            for c1 in range(COLS):
                if colcnt[c1] == 0:
                    continue
                s = 0
                top = bot = False
                for c2 in range(c1, COLS):
                    s += colsum[c2]
                    if num[r1][c2]:
                        top = True
                    if num[r2][c2]:
                        bot = True
                    if s > 10:
                        break
                    if s == 10 and colcnt[c2] and top and bot:
                        n += 1
    return n

# ---------------------------------------------------------------------------
# 엔진 채점 호출
# ---------------------------------------------------------------------------
def serialize(num, owner, P, played, depth, cap, budgets, include_pass, prev_pass):
    """side-to-move = P 를 player0 으로 정규화해 CLI 입력 문자열 생성"""
    out = ["NUM"]
    out += ["".join(str(num[r][c]) for c in range(COLS)) for r in range(ROWS)]
    out.append("OWN")
    for r in range(ROWS):
        row = []
        for c in range(COLS):
            o = owner[r][c]
            row.append("." if o == -1 else ("0" if o == P else "1"))
        out.append("".join(row))
    out.append(f"PLAYED {played[0]} {played[1]} {played[2]} {played[3]}")
    out.append(f"MAXDEPTH {depth}")
    out.append(f"CAPMS {cap}")
    out.append(f"INCLUDEPASS {1 if include_pass else 0}")
    out.append(f"PREVPASS {1 if prev_pass else 0}")
    out.append("BUDGETS " + " ".join(str(b) for b in budgets))
    return "\n".join(out) + "\n"

def grade(num, owner, P, played, depth, cap, budgets, include_pass, prev_pass):
    inp = serialize(num, owner, P, played, depth, cap, budgets, include_pass, prev_pass)
    res = subprocess.run([ENGINE_EXE], input=inp, capture_output=True, text=True, timeout=600)
    if res.returncode != 0:
        raise RuntimeError(f"analyze_engine 실패: {res.stderr.strip()}")
    return json.loads(res.stdout)

# decide() 의 턴당 예산 (submission/test.cpp Solver::budgetFor 포팅 — 분기게이트).
#   TUNE = self-play A/B 로 고른 추천 config(중반 긴장국면만 부스트). gateLo=gateHi=0 이면 기존 공식과 동치.
TUNE = dict(turnDiv=9, moveCap=1000, fracDiv=8, gateLo=8, gateHi=26,
            boost=250, boostCap=2000, boostFrac=4)
def decide_budget(t1, nz, nLegal):
    avail = max(1, t1 - 250)
    est_turns = max(4, nz // TUNE["turnDiv"])
    budget = avail // est_turns
    cap, frac = TUNE["moveCap"], TUNE["fracDiv"]
    if TUNE["gateLo"] <= nLegal <= TUNE["gateHi"]:   # 긴장 국면만 부스트
        budget = budget * TUNE["boost"] // 100
        cap, frac = TUNE["boostCap"], TUNE["boostFrac"]
    budget = min(budget, cap)
    budget = min(budget, t1 // frac)
    budget = min(budget, t1 // 3)                    # TLE 안전(항상)
    budget = max(30, budget)
    return min(budget, avail)

# ---------------------------------------------------------------------------
# 한 로그 분석
# ---------------------------------------------------------------------------
def analyze_log(path, depth, cap, budgets, include_pass):
    me = me_from_filename(path)
    init_rows, moves, sf, ss = parse_log(path)
    num, owner = new_board(init_rows)
    counts = [0, 0]
    my_time_used = 0
    records = []
    score_curve = []  # (turn, myScore, oppScore)

    for i, (player, r1, c1, r2, c2, t) in enumerate(moves):
        played = (r1, c1, r2, c2)
        is_me = (player == me)
        is_pass = (r1 < 0)
        # 직전(상대) 행동이 패스였는지 — 내가 지금 패스하면 연속패스로 종료. 엔진 prevWasPass 와 동일 의미.
        prev_pass = (i > 0 and moves[i - 1][1] < 0)
        nz_before = count_nz(num)
        t1_before = budget_actual = None
        sweep_budgets = []
        if is_me:
            t1_before = max(0, START_TIME_MS - my_time_used)
            budget_actual = decide_budget(t1_before, nz_before, count_legal(num))
            # 내 턴만 시간예산 스윕: 실제예산을 앵커로 + 기본 스윕 + cap, cap 이하로 정렬·중복제거.
            #   합법수 있는 수에서만 의미 있음(패스/강제수는 스킵해 시간 절약).
            if not is_pass:
                pts = sorted({budget_actual} | set(b for b in budgets if b <= cap) | {cap})
                sweep_budgets = [b for b in pts if b <= cap]

        sys.stderr.write(f"  [{i+1}/{len(moves)}] {'ME ' if is_me else 'OPP'} "
                         f"{'PASS' if is_pass else played} nz={nz_before}"
                         f"{' sweep='+str(sweep_budgets) if sweep_budgets else ''}\n")
        sys.stderr.flush()

        g = grade(num, owner, player, played, depth, cap, sweep_budgets, include_pass, prev_pass)

        rec = {
            "turn": i + 1, "player": player, "is_me": is_me, "is_pass": is_pass,
            "move": played, "time": t, "nz_before": nz_before,
            "t1_before": t1_before, "budget_actual": budget_actual,
            "nLegal": g["nLegal"], "usedDepth": g["usedDepth"],
            "bestMove": g["bestMove"], "bestEval": g["bestEval"],
            "rank": g["played"]["rank"], "found": g["played"]["found"],
            "qualityPct": g["played"]["qualityPct"], "cellLoss": g["played"]["cellLoss"],
            "convDepth": g["convergenceDepth"], "sweep": g["budgetSweep"],
        }
        # 시간예산 최적 임계: cellLoss<=0 인 가장 작은 예산 + 그 깊이
        sweep = g["budgetSweep"]
        opt_budget = opt_depth = None
        for s in sweep:
            if s["cellLoss"] is not None and s["cellLoss"] <= 0.0:
                opt_budget, opt_depth = s["ms"], s["depth"]
                break
        rec["opt_budget"] = opt_budget
        rec["opt_depth"] = opt_depth
        # 실제예산(budget_actual)에서 엔진이 둘 수의 cellLoss·도달깊이 (스윕 앵커)
        cl_actual = depth_actual = None
        for s in sweep:
            if s["ms"] == budget_actual:
                cl_actual, depth_actual = s["cellLoss"], s["depth"]
                break
        rec["cl_actual"] = cl_actual          # 실제 예산에서 엔진 선택수의 손해(칸)
        rec["depth_actual"] = depth_actual    # 실제 예산에서 완료한 탐색 깊이
        # 시간 레버리지: 실제예산에선 손해(>0)인데 현실적 예산(opt_budget<=affordable)이면 손해0이 되는 수.
        #   → "여기 시간을 더 썼다면 더 좋은 수를 뒀다"는 정량 신호.
        # 레버리지 = 실제예산에선 손해(>0)인데, 더 큰(현실적) 예산이면 손해0. opt>actual 가드 필수
        #   (새 예산공식이 이미 부스트 구간이면 opt<=actual → '시간으로 회수 불가(구조적)' 이므로 제외).
        leverage = (is_me and not is_pass and cl_actual is not None and cl_actual > 0.0
                    and opt_budget is not None and opt_budget <= LEVERAGE_AFFORDABLE_MS
                    and budget_actual is not None and opt_budget > budget_actual)
        rec["leverage"] = leverage
        rec["recoverable"] = (cl_actual if leverage else 0.0)  # 그 예산 늘릴 때 회수 칸수
        # 시간압박 추정: 블런더인데 (실제예산<최적임계) 또는 (예산스윕없이 저예산에서 발생)
        tp = False
        if is_me and rec["cellLoss"] >= BLUNDER_CELL:
            if opt_budget is not None:
                tp = budget_actual is not None and budget_actual < opt_budget
            else:
                tp = budget_actual is not None and budget_actual <= 150
        rec["time_pressure"] = tp
        records.append(rec)

        play_move(num, owner, counts, played, player)
        score_curve.append((i + 1, counts[me], counts[1 - me]))
        if is_me:
            my_time_used += t   # 내 누적 소요시간(실제 로그값) -> 다음 내 턴의 잔여시간 t1_before 계산

    # 리플레이 정합성 게이트
    recon = (counts[0], counts[1])
    logged = (sf, ss)
    gate_ok = (recon == logged)

    final_my, final_opp = counts[me], counts[1 - me]
    result = "WIN" if final_my > final_opp else ("LOSS" if final_my < final_opp else "DRAW")
    return {
        "path": path, "me": me, "result": result,
        "final_my": final_my, "final_opp": final_opp,
        "score_first": sf, "score_second": ss,
        "recon_first": counts[0], "recon_second": counts[1], "gate_ok": gate_ok,
        "records": records, "score_curve": score_curve,
    }

# ---------------------------------------------------------------------------
# 리포트 (결정론 마크다운)
# ---------------------------------------------------------------------------
def fmt_move(m):
    return "PASS" if m[0] < 0 else f"({m[0]},{m[1]})-({m[2]},{m[3]})"

def compute_time_audit(recs):
    """내 수의 시간 사용/슬랙/국면분포/레버리지 집계. (record 들에 이미 채워진 필드 사용)"""
    my = [r for r in recs if r["is_me"]]
    opp = [r for r in recs if not r["is_me"]]
    my_time = sum(r["time"] for r in my)
    opp_time = sum(r["time"] for r in opp)
    slack = START_TIME_MS - my_time
    # 국면 3분할(내 수 순서 기준)
    n = len(my)
    bounds = [0, n // 3, (2 * n) // 3, n]
    phases = []
    for k, label in enumerate(("초반", "중반", "종반")):
        seg = my[bounds[k]:bounds[k + 1]]
        phases.append((label, len(seg), sum(r["time"] for r in seg)))
    leverage = [r for r in my if r.get("leverage")]
    total_recoverable = sum(r.get("recoverable", 0.0) for r in my)
    extra_ms = sum((r["opt_budget"] - r["budget_actual"]) for r in leverage
                   if r.get("opt_budget") is not None and r.get("budget_actual") is not None)
    return {
        "my_time": my_time, "opp_time": opp_time, "slack": slack,
        "slack_pct": (100.0 * slack / START_TIME_MS),
        "my_max": max((r["time"] for r in my), default=0),
        "opp_max": max((r["time"] for r in opp), default=0),
        "my_avg": (my_time / n if n else 0), "opp_avg": (opp_time / len(opp) if opp else 0),
        "phases": phases, "leverage": leverage,
        "total_recoverable": total_recoverable, "extra_ms": extra_ms,
    }

def build_report(a):
    me_s = "FIRST(선공)" if a["me"] == 0 else "SECOND(후공)"
    recs = a["records"]
    my = [r for r in recs if r["is_me"]]
    opp = [r for r in recs if not r["is_me"]]
    my_loss = sum(r["cellLoss"] for r in my)
    opp_loss = sum(r["cellLoss"] for r in opp)
    my_blunders = sorted([r for r in my if r["cellLoss"] >= BLUNDER_CELL],
                         key=lambda r: r["cellLoss"], reverse=True)
    turning = max(my, key=lambda r: r["cellLoss"]) if my else None
    opp_optimal = sum(1 for r in opp if r["cellLoss"] <= 0.0)
    ta = compute_time_audit(recs)

    L = []
    L.append(f"# 패배 분석 — {os.path.basename(a['path'])}")
    L.append("")
    L.append(f"- 내 엔진: **{me_s}**")
    L.append(f"- 결과: **{a['result']}**  (내 점수 {a['final_my']} : 상대 {a['final_opp']})")
    g = "✅ 일치" if a["gate_ok"] else f"❌ 불일치 (재구성 {a['recon_first']}:{a['recon_second']} vs 로그 {a['score_first']}:{a['score_second']})"
    L.append(f"- 리플레이 정합성: {g}")
    L.append("")
    L.append("## 요약")
    L.append("")
    L.append(f"- **내 누적 손해**: {my_loss:.1f}칸  /  **상대 누적 손해**: {opp_loss:.1f}칸")
    L.append(f"  - 두 값의 차이({my_loss-opp_loss:+.1f}칸)가 엔진 관점의 '수 손해' 격차다. "
             f"실제 점수차({a['final_my']-a['final_opp']:+d})와 비교해 패인이 **수 실수**인지 "
             f"**초기/구조적 불리**인지 가늠한다.")
    L.append(f"- 내 블런더(≥{BLUNDER_CELL:.0f}칸 손해): **{len(my_blunders)}회**")
    if turning and turning["cellLoss"] >= BLUNDER_CELL:
        L.append(f"- **전환점**: {turning['turn']}수 {fmt_move(turning['move'])} "
                 f"— 최선 {fmt_move(turning['bestMove'])} 대비 **{turning['cellLoss']:.1f}칸** 손해"
                 f"{' (시간압박 추정)' if turning['time_pressure'] else ''}")
    else:
        L.append("- **전환점**: 두드러진 단일 블런더 없음 — 누적 열세/초기 불리 가능성")
    L.append(f"- 상대 최적수 비율: {opp_optimal}/{len(opp)} "
             f"({(100.0*opp_optimal/len(opp)) if opp else 0:.0f}% 손해0)")
    L.append("")

    # ── 시간 사용 감사 ────────────────────────────────────────────────────
    L.append("## 시간 사용 감사 (어느 구간에 시간을 더 쓸 수 있나)")
    L.append("")
    L.append(f"- **내 총 사용 시간: {ta['my_time']} ms / 예산 {START_TIME_MS} ms** "
             f"→ **미사용 슬랙 {ta['slack']} ms ({ta['slack_pct']:.0f}%)**")
    L.append(f"- 상대 총 사용: {ta['opp_time']} ms "
             f"(상대는 슬랙을 {100.0*(START_TIME_MS-ta['opp_time'])/START_TIME_MS:.0f}% 만 남김)")
    L.append(f"- 내 수당 평균/최대: {ta['my_avg']:.0f} / {ta['my_max']} ms  ·  "
             f"상대 평균/최대: {ta['opp_avg']:.0f} / {ta['opp_max']} ms "
             f"— 상대가 핵심 수에 시간을 **몰아쓰는지**(최대값)와 대조한다.")
    ph = "  ·  ".join(f"{lab} {tt}ms({cnt}수)" for (lab, cnt, tt) in ta["phases"])
    L.append(f"- 국면별 내 사용시간: {ph}")
    if ta["slack"] > 1500:
        L.append(f"- ⏰ **슬랙이 {ta['slack']}ms 나 남았다** — 예산 공식이 시간을 과소 사용 중. "
                 f"아래 '시간 레버리지' 가 그 시간을 **어디에 쓰면 이득**인지 보여준다.")
    L.append("")

    # ── 시간 레버리지: '시간 더 쓰면 더 좋은 수' 후보 ──────────────────────
    L.append("## 시간 레버리지 (예산을 늘리면 손해가 사라지는 수)")
    L.append("")
    L.append("> 각 내 수에서 `decide()` 를 실제예산 / 더 큰 예산들 / cap 으로 재실행해, "
             "엔진이 그 예산에서 두는 수의 손해(칸)·도달깊이를 본다. "
             "실제예산에선 손해(>0)인데 현실적 예산(≤"
             f"{LEVERAGE_AFFORDABLE_MS}ms)으로 늘리면 손해0이 되는 수 = **시간 레버리지**.")
    L.append("")
    lev = sorted(ta["leverage"], key=lambda r: r.get("recoverable", 0.0), reverse=True)
    if lev:
        L.append("| 수 | 둔 수 | 실제ms | 예산ms | 깊이@예산 | 손해@예산(칸) | opt예산ms | 깊이@opt | 수렴깊이 | 회수(칸) |")
        L.append("|---|---|---|---|---|---|---|---|---|---|")
        for r in lev:
            cd = r["convDepth"] if r["convDepth"] is not None else "-"
            da = r["depth_actual"] if r["depth_actual"] is not None else "-"
            od = r["opt_depth"] if r["opt_depth"] is not None else "-"
            cla = r["cl_actual"] if r["cl_actual"] is not None else 0.0
            L.append(f"| {r['turn']} | {fmt_move(r['move'])} | {r['time']} | {r['budget_actual']} | "
                     f"{da} | {cla:.1f} | {r['opt_budget']} | {od} | {cd} | {r['recoverable']:.1f} |")
        L.append("")
        L.append(f"- **레버리지 수 {len(lev)}개**, 추가로 약 **{ta['extra_ms']} ms** 만 더 썼다면 "
                 f"엔진 기준 약 **{ta['total_recoverable']:.1f}칸** 회수 가능 "
                 f"(미사용 슬랙 {ta['slack']}ms 안에서 충분).")
    else:
        L.append("_레버리지 수 없음 — 실제예산에서 이미 최적이거나, 더 줘도 같은 수를 둠. "
                 "그렇다면 패인은 시간배분이 아니라 평가/탐색 구조 쪽이다._")
    L.append("")

    # 블런더 표
    L.append("## 내 블런더 (손해 큰 순)")
    L.append("")
    if my_blunders:
        L.append("| 수 | 둔 수 | 최선 | 손해(칸) | 수준% | 순위 | 수렴깊이 | 남은ms | 예산ms | 시간압박 |")
        L.append("|---|---|---|---|---|---|---|---|---|---|")
        for r in my_blunders[:12]:
            cd = r["convDepth"] if r["convDepth"] is not None else "-"
            ob = "" if r["opt_budget"] is None else f"≥{r['opt_budget']}"
            L.append(f"| {r['turn']} | {fmt_move(r['move'])} | {fmt_move(r['bestMove'])} | "
                     f"{r['cellLoss']:.1f} | {r['qualityPct']:.0f} | {r['rank']}/{r['nLegal']+1} | "
                     f"{cd} | {r['t1_before']} | {r['budget_actual']} | "
                     f"{'⚠'+ob if r['time_pressure'] else (ob or '-')} |")
    else:
        L.append("_블런더 없음._")
    L.append("")

    # 점수 추이
    L.append("## 점수 추이 (내 점수 − 상대 점수)")
    L.append("")
    diffs = [(t, my_s - op_s) for (t, my_s, op_s) in a["score_curve"]]
    spark = " ".join(f"{d:+d}" for (_, d) in diffs[::2])  # 격수 샘플
    L.append(f"`{spark}`")
    L.append("")

    # 전체 수별 표 (부록)
    L.append("## 부록 — 전수 채점")
    L.append("")
    L.append("| 수 | 주체 | 둔 수 | 최선 | 손해 | 수준% | 순위/합법 | 깊이 |")
    L.append("|---|---|---|---|---|---|---|---|")
    for r in recs:
        who = "나" if r["is_me"] else "상대"
        L.append(f"| {r['turn']} | {who} | {fmt_move(r['move'])} | {fmt_move(r['bestMove'])} | "
                 f"{r['cellLoss']:.1f} | {r['qualityPct']:.0f} | {r['rank']}/{r['nLegal']+1} | {r['usedDepth']} |")
    L.append("")
    return "\n".join(L), {
        "my_loss": my_loss, "opp_loss": opp_loss, "my_blunders": my_blunders,
        "turning": turning, "opp_optimal": opp_optimal, "opp_n": len(opp),
        "time_audit": ta,
    }

# ---------------------------------------------------------------------------
# Gemini 해설 (옵션)
# ---------------------------------------------------------------------------
def gemini_narrate(a, summary, model):
    import urllib.request
    key = os.environ.get("GEMINI_API_KEY")
    if not key:
        return None
    me_s = "선공(FIRST)" if a["me"] == 0 else "후공(SECOND)"
    blunder_lines = "\n".join(
        f"- {r['turn']}수 {fmt_move(r['move'])} → 최선 {fmt_move(r['bestMove'])}, "
        f"{r['cellLoss']:.1f}칸 손해, 수준 {r['qualityPct']:.0f}%"
        f"{', 시간압박' if r['time_pressure'] else ''}"
        for r in summary["my_blunders"][:8]
    ) or "(없음)"
    t = summary["turning"]
    turning_s = (f"{t['turn']}수 {fmt_move(t['move'])} (최선 {fmt_move(t['bestMove'])}, "
                 f"{t['cellLoss']:.1f}칸 손해)") if t else "없음"
    ta = summary.get("time_audit", {})
    lev = ta.get("leverage", [])
    lev_lines = "\n".join(
        f"- {r['turn']}수 {fmt_move(r['move'])}: 실제예산 {r['budget_actual']}ms에선 {r['cl_actual']:.1f}칸 손해, "
        f"{r['opt_budget']}ms로 늘리면 손해0 (회수 {r['recoverable']:.1f}칸)"
        for r in sorted(lev, key=lambda r: r.get("recoverable", 0.0), reverse=True)[:6]
    ) or "(없음 — 더 줘도 같은 수)"
    time_s = (f"내 총 사용 {ta.get('my_time')}ms / 예산 {START_TIME_MS}ms (미사용 슬랙 {ta.get('slack')}ms, "
              f"{ta.get('slack_pct',0):.0f}%). 상대 총 사용 {ta.get('opp_time')}ms, 상대 수당 최대 {ta.get('opp_max')}ms "
              f"vs 내 최대 {ta.get('my_max')}ms.") if ta else "(시간 데이터 없음)"
    prompt = f"""너는 2인 완전정보 보드게임 '버섯밭 점령'(직사각형 합10 점령, 칸 많은 쪽 승) 봇의 코치다.
아래는 내 엔진이 진 한 대국을, '내 엔진 자신'으로 매 수를 재채점한 객관 데이터다.
(cellLoss=최선 대비 손해 칸수, 수준%=후보수 평가 정규화, 높을수록 좋음. 평가의 1칸=실제 1점.)

대국: 내 엔진은 {me_s}. 결과 패배 (내 {a['final_my']} : 상대 {a['final_opp']}).
내 누적 수손해 {summary['my_loss']:.1f}칸, 상대 누적 수손해 {summary['opp_loss']:.1f}칸.
상대 최적수 비율 {summary['opp_optimal']}/{summary['opp_n']}.
전환점: {turning_s}
내 주요 블런더:
{blunder_lines}

[시간 사용]
{time_s}
시간 레버리지(예산 늘리면 손해가 사라지는 수):
{lev_lines}

다음을 한국어로, 군더더기 없이 답하라:
1) 패인 한 줄 요약 (수 실수 누적인지, 초기/구조적 열세인지, 아니면 시간 과소사용인지 — 손해 비교 + 슬랙으로 판단).
2) 가장 치명적인 1~2개 수와 그 이유(가능하면 좌표 기준).
3) **시간배분** 개선점: 슬랙이 크거나 레버리지 수가 있으면 "어느 구간에 시간을 더 써야 하는지" 구체적으로.
4) 다음 대국을 위한 구체적 행동 1~2가지(decideBudget 등 예산 정책 포함).
"""
    body = json.dumps({"contents": [{"parts": [{"text": prompt}]}]}).encode("utf-8")
    url = f"https://generativelanguage.googleapis.com/v1beta/models/{model}:generateContent?key={key}"
    req = urllib.request.Request(url, data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        return data["candidates"][0]["content"]["parts"][0]["text"]
    except Exception as e:
        return f"_(Gemini 호출 실패: {e})_"

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
def main():
    load_dotenv()  # logAnalyzer/.env 의 GEMINI_API_KEY / GEMINI_MODEL 자동 로딩
    ap = argparse.ArgumentParser(description="패배 로그 분석기")
    ap.add_argument("logs", nargs="*", help="로그 파일(미지정 시 폴더 내 *.txt 전부)")
    ap.add_argument("--depth", type=int, default=18)
    ap.add_argument("--cap", type=int, default=4000, help="포지션당 시간상한 ms")
    ap.add_argument("--budgets", default="",
                    help=f"시간예산 스윕(쉼표, 내 턴만). 미지정 시 기본 {DEFAULT_SWEEP} 사용.")
    ap.add_argument("--no-sweep", action="store_true",
                    help="시간예산 스윕 비활성(빠름, 시간감사 표가 비어짐)")
    ap.add_argument("--with-pass", action="store_true",
                    help="합법수 있어도 PASS 를 후보에 포함(주의: 상대 패스 미모델링 → 과대평가)")
    ap.add_argument("--no-llm", action="store_true")
    ap.add_argument("--model", default=os.environ.get("GEMINI_MODEL", DEFAULT_MODEL))
    ap.add_argument("--check-llm", action="store_true",
                    help="Gemini 키 유효성 + 사용 가능한 모델 목록만 확인하고 종료")
    args = ap.parse_args()

    if args.check_llm:
        check_llm(args.model)
        return

    if not os.path.exists(ENGINE_EXE):
        sys.exit(f"엔진이 없다. 먼저 빌드: g++ -O2 -std=c++17 engine/analyze_engine.cpp -o {ENGINE_EXE}")

    # 시간감사가 핵심이므로 예산 스윕을 기본 동작으로(미지정 시 DEFAULT_SWEEP). --no-sweep 으로 끔.
    if args.no_sweep:
        budgets = []
    elif args.budgets:
        budgets = [int(x) for x in args.budgets.split(",") if x.strip()]
    else:
        budgets = list(DEFAULT_SWEEP)
    logs = args.logs or sorted(glob.glob(os.path.join(SCRIPT_DIR, "*.txt")))
    if not logs:
        sys.exit("분석할 로그가 없다.")
    os.makedirs(REPORT_DIR, exist_ok=True)

    for path in logs:
        if not os.path.isabs(path) and not os.path.exists(path):
            path = os.path.join(SCRIPT_DIR, path)
        sys.stderr.write(f"[분석] {os.path.basename(path)}\n")
        a = analyze_log(path, args.depth, args.cap, budgets, args.with_pass)
        if not a["gate_ok"]:
            sys.stderr.write(f"  ⚠ 리플레이 정합성 실패! 재구성 {a['recon_first']}:{a['recon_second']} "
                             f"≠ 로그 {a['score_first']}:{a['score_second']}\n")
        report, summary = build_report(a)
        if not args.no_llm:
            sys.stderr.write("  Gemini 해설 호출...\n")
            narrative = gemini_narrate(a, summary, args.model)
            if narrative:
                report += "\n## LLM 해설 (Gemini)\n\n" + narrative + "\n"
            else:
                report += "\n## LLM 해설\n\n_GEMINI_API_KEY 미설정 — 결정론 리포트만 생성._\n"
        base = os.path.splitext(os.path.basename(path))[0].replace("#", "_")
        out = os.path.join(REPORT_DIR, base + ".md")
        with open(out, "w", encoding="utf-8") as f:
            f.write(report)
        sys.stderr.write(f"  → {out}\n")

if __name__ == "__main__":
    main()

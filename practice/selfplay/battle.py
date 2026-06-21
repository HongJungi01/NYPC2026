#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
battle.py — 두 봇 exe를 N판 맞붙여 승패만 집계.

NYPC 2026 버섯밭 점령 게임. 프로토콜(GAME_RULES §5)을 직접 구현한 자체 심판으로
임의의 두 바이너리를 대국시킨다. 매 판 새 랜덤 보드를 만들고, 선/후공을 교대해 공정 비교.

사용:
  python battle.py <exeA> <exeB> <N>            # N판 (기본 시간 10000ms/측)
  python battle.py ../submission/onlypass.exe ../submission/full.exe 20
  옵션: --time MS(판당 측당 누적시간, 기본 10000) --seed S(보드 시드, 기본 랜덤)

규칙: 매 수 §3 검증(합10 + 4변 버섯), 소유권 탈취 반영, 누적 10s 초과/무효수/크래시 = 패,
      연속 2패스 → 소유칸 수로 승패.
"""
import argparse, os, random, subprocess, sys, threading, time
from concurrent.futures import ThreadPoolExecutor, as_completed

NR, NC = 10, 17
START_MS = 10000
TIME_GRACE_MS = 1000
MINGW = r"C:\msys64\mingw64\bin"


class Bot:
    """봇 1개 = 자식 프로세스. stdin/stdout 라인 파이프."""
    def __init__(self, exe):
        self.exe = os.path.abspath(exe)
        self.name = os.path.basename(exe)
        env = os.environ.copy()
        if os.path.isdir(MINGW):                      # mingw 런타임 DLL 경로 보강(있으면)
            env["PATH"] = MINGW + os.pathsep + env.get("PATH", "")
        self.env = env
        self.p = None

    def start(self):
        self.p = subprocess.Popen(
            [self.exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL, env=self.env, text=True, bufsize=1,
            cwd=os.path.dirname(self.exe))

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def recv(self, timeout):
        box = [None]
        def rd():
            try:
                box[0] = self.p.stdout.readline()
            except Exception:
                box[0] = None
        t = threading.Thread(target=rd, daemon=True)
        t.start()
        t.join(timeout)
        if t.is_alive():
            return None                               # 타임아웃
        return box[0].strip() if box[0] else None     # None = EOF/크래시

    def close(self):
        if not self.p:
            return
        try:
            self.send("FINISH")
            self.p.stdin.close()
        except Exception:
            pass
        try:
            self.p.wait(timeout=2)
        except Exception:
            self.p.kill()


# ─── 보드/규칙 ───
def gen_board(rng):
    return ["".join(str(rng.randint(1, 9)) for _ in range(NC)) for _ in range(NR)]


def is_valid(num, r1, c1, r2, c2):
    if not (0 <= r1 <= r2 < NR and 0 <= c1 <= c2 < NC):
        return False
    s = 0
    top = bot = left = right = False
    for r in range(r1, r2 + 1):
        for c in range(c1, c2 + 1):
            v = num[r][c]
            if v:
                s += v
                if r == r1: top = True
                if r == r2: bot = True
                if c == c1: left = True
                if c == c2: right = True
    return s == 10 and top and bot and left and right


def apply_move(num, owner, player, r1, c1, r2, c2, counts):
    for r in range(r1, r2 + 1):
        for c in range(c1, c2 + 1):
            if num[r][c] != 0:
                num[r][c] = 0
                owner[r][c] = player
                counts[player] += 1
            elif owner[r][c] != player:
                prev = owner[r][c]
                owner[r][c] = player
                counts[player] += 1
                if prev:
                    counts[prev] -= 1


# ─── 한 판 ───  (first/second = Bot 객체)  반환: ('first'|'second'|'draw', sf, ss, reason)
def play_game(first, second, board, total_ms):
    num = [[int(ch) for ch in board[r]] for r in range(NR)]
    owner = [[0] * NC for _ in range(NR)]
    counts = {1: 0, 2: 0}
    bots = {1: first, 2: second}
    remaining = {1: float(total_ms), 2: float(total_ms)}

    for pid, role in ((1, "FIRST"), (2, "SECOND")):
        bots[pid].start()
        bots[pid].send("READY " + role)
        if bots[pid].recv(3.0) != "OK":
            return (_loser_to_winner(pid), counts[1], counts[2], f"READY실패({bots[pid].name})")
    init_line = "INIT " + " ".join(board)
    bots[1].send(init_line)
    bots[2].send(init_line)

    cur, passes, ply = 1, 0, 0
    for ply in range(1, 600):
        opp = 2 if cur == 1 else 1
        bots[cur].send(f"TIME {int(round(remaining[cur]))} {int(round(remaining[opp]))}")
        t0 = time.perf_counter()
        line = bots[cur].recv(max(0.5, remaining[cur] / 1000.0) + 2.0)
        remaining[cur] -= (time.perf_counter() - t0) * 1000.0

        if line is None:
            return (_loser_to_winner(cur), counts[1], counts[2], f"무응답({bots[cur].name})")
        if remaining[cur] < -TIME_GRACE_MS:
            return (_loser_to_winner(cur), counts[1], counts[2], f"시간초과({bots[cur].name})")
        try:
            r1, c1, r2, c2 = map(int, line.split()[:4])
        except Exception:
            return (_loser_to_winner(cur), counts[1], counts[2], f"파싱불가({bots[cur].name})")

        if r1 < 0:
            passes += 1
        else:
            if not is_valid(num, r1, c1, r2, c2):
                return (_loser_to_winner(cur), counts[1], counts[2], f"무효수({bots[cur].name})")
            apply_move(num, owner, cur, r1, c1, r2, c2, counts)
            passes = 0
        bots[opp].send(f"OPP {r1} {c1} {r2} {c2} 0")
        if passes >= 2:
            break
        cur = opp

    sf, ss = counts[1], counts[2]
    winner = "first" if sf > ss else ("second" if ss > sf else "draw")
    return (winner, sf, ss, "정상")


def _loser_to_winner(loser_pid):
    return "second" if loser_pid == 1 else "first"


def _play_one(exeA, exeB, board, total_ms, i):
    """배틀 1판(스레드 워커). 선/후공은 판 인덱스로 교대. 결과 dict 반환."""
    if i % 2 == 0:
        first, second, a_is_first = Bot(exeA), Bot(exeB), True
    else:
        first, second, a_is_first = Bot(exeB), Bot(exeA), False
    try:
        winner, sf, ss, reason = play_game(first, second, board, total_ms)
    except Exception as e:                              # 예외도 한 판 결과로 흡수(전체 중단 방지)
        return dict(i=i, first=first.name, a_first=a_is_first,
                    winner="draw", sf=0, ss=0, reason=f"오류:{e}")
    finally:
        first.close()
        second.close()
    return dict(i=i, first=first.name, a_first=a_is_first,
                winner=winner, sf=sf, ss=ss, reason=reason)


def run_battle(exeA, exeB, count, total_ms, seed, jobs):
    rng = random.Random(seed)
    boards = [gen_board(rng) for _ in range(count)]    # 미리 결정적 생성(병렬 완료순서와 무관히 재현)
    nameA, nameB = os.path.basename(exeA), os.path.basename(exeB)
    jobs = max(1, min(jobs, count))
    print(f"battle: {nameA}  vs  {nameB}   N={count}  time={total_ms}ms/측  seed={seed}  parallel={jobs}")

    results = [None] * count
    lock = threading.Lock()
    done = 0
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        futs = [ex.submit(_play_one, exeA, exeB, boards[i], total_ms, i) for i in range(count)]
        for fut in as_completed(futs):
            r = fut.result()
            results[r["i"]] = r
            res = "무" if r["winner"] == "draw" else \
                  ("A" if (r["winner"] == "first") == r["a_first"] else "B")
            with lock:
                done += 1
                print(f"  [{done:>3}/{count}] #{r['i']+1:<3} {r['first']:>12}(선) "
                      f"{r['sf']:>4}:{r['ss']:<4} → 승자 {res:<4} {r['reason']}")

    # 인덱스 순서로 집계 (A 기준)
    wA = lA = d = abn = 0
    for r in results:
        if r["reason"] != "정상":
            abn += 1
        if r["winner"] == "draw":
            d += 1
        elif (r["winner"] == "first") == r["a_first"]:
            wA += 1
        else:
            lA += 1
    print("-" * 60)
    print(f"  {nameA:<22} : {wA}승 {lA}패 {d}무")
    print(f"  {nameB:<22} : {lA}승 {wA}패 {d}무")
    if abn:
        print(f"  (비정상 종료 {abn}판 — 무효수/시간초과/크래시 포함)")
    return 0


def main():
    ap = argparse.ArgumentParser(description="두 봇 exe를 N판 맞붙여 승패 집계")
    ap.add_argument("exeA")
    ap.add_argument("exeB")
    ap.add_argument("count", type=int, help="배틀 횟수")
    ap.add_argument("--time", type=int, default=START_MS, help="판당 측당 누적시간 ms (기본 10000)")
    ap.add_argument("--seed", type=int, default=None, help="보드 시드 (생략 시 랜덤)")
    ap.add_argument("--jobs", "-j", type=int, default=4, help="동시 배틀 수 (기본 4). 코어 수 이하 권장")
    args = ap.parse_args()
    seed = args.seed if args.seed is not None else random.randrange(1 << 30)
    for p in (args.exeA, args.exeB):
        if not os.path.exists(p):
            print(f"[오류] 파일 없음: {p}", file=sys.stderr)
            return 2
    return run_battle(args.exeA, args.exeB, args.count, args.time, seed, args.jobs)


if __name__ == "__main__":
    sys.exit(main())

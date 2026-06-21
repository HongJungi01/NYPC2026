#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ponder_depth_probe.py — 폰더가 '같은 예산에 더 깊이' 보는지 측정(임시 진단 도구).

봇(FIRST)을 구동하고, 이 스크립트가 상대(SECOND) 역할을 한다. 매 상대 턴마다
WINDOW_MS 만큼 '고민하는 척' sleep 한 뒤 OPP 를 보낸다 → 그 시간이 봇의 폰더 창이 된다.
봇을 PONDER_DEBUG=1 로 띄워 매 수 도달깊이(reachedDepth)를 stderr 로 받고, 폰더 ON/OFF
두 번 돌려 평균 깊이를 비교한다.

사용: python ponder_depth_probe.py <exe> [WINDOW_MS] [MOVES]
"""
import os, sys, subprocess, threading, time, random, tempfile, re

NR, NC = 10, 17

def gen_board(rng):
    return ["".join(str(rng.randint(1, 9)) for _ in range(NC)) for _ in range(NR)]

def is_valid(num, r1, c1, r2, c2):
    s = 0; top = bot = left = right = False
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

def apply_move(num, owner, player, r1, c1, r2, c2, cnt):
    for r in range(r1, r2 + 1):
        for c in range(c1, c2 + 1):
            if num[r][c] != 0:
                num[r][c] = 0; owner[r][c] = player; cnt[player] += 1
            elif owner[r][c] != player:
                p = owner[r][c]; owner[r][c] = player; cnt[player] += 1
                if p: cnt[p] -= 1

def first_legal(num):
    # 강한 상대 근사: 합10 직사각형 중 '점령 칸수 최대'(greedy). 봇의 무브오더링 key 와 같은 기준이라
    # 폰더가 펼친 PV(최선수)와 자주 일치 → 현실적 ponder-hit 를 반영한다.
    best = None; bestcnt = -1
    for r1 in range(NR):
        for r2 in range(r1, NR):
            for c1 in range(NC):
                for c2 in range(c1, NC):
                    if is_valid(num, r1, c1, r2, c2):
                        cnt = sum(1 for r in range(r1, r2 + 1) for c in range(c1, c2 + 1) if num[r][c])
                        if cnt > bestcnt: bestcnt = cnt; best = (r1, c1, r2, c2)
    return best

def recv(p, timeout):
    box = [None]
    def rd(): box[0] = p.stdout.readline()
    t = threading.Thread(target=rd, daemon=True); t.start(); t.join(timeout)
    if t.is_alive(): return None
    return box[0].strip() if box[0] else None

def run(exe, board, window_ms, moves, ponder_off):
    env = os.environ.copy()
    env["PATH"] = r"C:\msys64\mingw64\bin" + os.pathsep + env.get("PATH", "")
    env["PONDER_DEBUG"] = "1"
    if ponder_off: env["PONDER_OFF"] = "1"
    elif "PONDER_OFF" in env: del env["PONDER_OFF"]
    errf = tempfile.NamedTemporaryFile(mode="w+", delete=False, suffix=".err")
    p = subprocess.Popen([os.path.abspath(exe)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                         stderr=errf, env=env, text=True, bufsize=1)
    def snd(s): p.stdin.write(s + "\n"); p.stdin.flush()
    num = [[int(ch) for ch in board[r]] for r in range(NR)]
    owner = [[0] * NC for _ in range(NR)]; cnt = {1: 0, 2: 0}
    rem = 10000.0
    snd("READY FIRST"); recv(p, 3.0)
    snd("INIT " + " ".join(board))
    for _ in range(moves):
        snd(f"TIME {int(rem)} 10000")
        t0 = time.perf_counter()
        line = recv(p, max(0.5, rem / 1000.0) + 2.0)
        rem -= (time.perf_counter() - t0) * 1000.0
        if line is None: break
        r1, c1, r2, c2 = map(int, line.split()[:4])
        if r1 >= 0: apply_move(num, owner, 1, r1, c1, r2, c2, cnt)
        opp = first_legal(num)                       # 상대(나) 응수
        time.sleep(window_ms / 1000.0)               # '고민' = 봇의 폰더 창
        if opp is None:
            snd("OPP -1 -1 -1 -1 0")
        else:
            apply_move(num, owner, 2, *opp, cnt); snd(f"OPP {opp[0]} {opp[1]} {opp[2]} {opp[3]} 0")
    try: snd("FINISH"); p.wait(timeout=3)
    except Exception: p.kill()
    errf.flush(); errf.seek(0); txt = errf.read(); errf.close(); os.unlink(errf.name)
    return [int(m) for m in re.findall(r"depth=(\d+)", txt)]

def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "../submission/ponder.exe"
    window = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    moves = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    board = gen_board(random.Random(32399))
    on = run(exe, board, window, moves, ponder_off=False)
    off = run(exe, board, window, moves, ponder_off=True)
    n = min(len(on), len(off))
    print(f"window={window}ms  moves={n}")
    print(f"  ponder OFF depth/move: {off[:n]}  (avg {sum(off[:n])/n:.1f})")
    print(f"  ponder ON  depth/move: {on[:n]}  (avg {sum(on[:n])/n:.1f})")
    print(f"  Δ평균깊이(ON-OFF) = {(sum(on[:n])-sum(off[:n]))/n:+.1f}")

if __name__ == "__main__":
    main()

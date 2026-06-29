#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
probe_aggregate.py — PROBE_MAIN(test.cpp) 오라클 CSV 집계.

`g++ -O2 -std=c++17 -DPROBE_MAIN submission/test.cpp -o probe.exe && ./probe.exe > probe.csv`
의 출력(pos,nz,nLegal,depth,value,best_enc,nodes,ms,reached_max)을 받아
nz 버킷별로 다음을 산출한다:

  A. 수렴/도달 — 둔 수가 안 바뀌는 깊이(move/value convergenceDepth) vs nz·nLegal,
     캡 안 완전풀림 비율, 도달한 최대 깊이.            → ①예산: "이 nz면 몇 depth면 충분한가"
  B. 완전풀림 비용 — solved 포지션의 ms/nodes/depth 중앙값 vs nz.
     (nz↑일수록 지수증가 — 어느 nz부터 '그냥 풀고 시간 은행' 가능한지 임계)   → ①예산
  C. material-only 오차 — solved 포지션에서 d-ply 탐색값이 정확 최종값과
     몇 칸 어긋나는지 vs depth.                          → ③평가: leaf 휴리스틱 필요성 정량화

머신속도 의존인 ms 보다 depth 지표가 1순위(README 경고). value 1칸 = W_MAT(100).

사용: python selfplay/probe_aggregate.py probe.csv [--bucket 10]
"""
import argparse
import csv
import statistics
import sys
from collections import defaultdict

# Windows 한글 콘솔(cp949)에서 비ASCII/em-dash 출력 시 크래시 방지 — 콘솔 출력을 UTF-8 로.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

W_MAT = 100
GAP_DEPTHS = [4, 6, 8, 10, 12]


def med(xs):
    xs = [x for x in xs if x is not None]
    return statistics.median(xs) if xs else None


def fmt(x, nd=1):
    return "-" if x is None else (f"{x:.{nd}f}" if isinstance(x, float) else str(x))


def load_positions(path):
    """CSV → {pos: {nz,nLegal,reached_max, depths:[(d,value,best_enc,nodes,ms)...]}}"""
    pos = defaultdict(lambda: {"depths": []})
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            p = int(row["pos"])
            rec = pos[p]
            rec["nz"] = int(row["nz"])
            rec["nLegal"] = int(row["nLegal"])
            rec["reached_max"] = int(row["reached_max"])
            rec["depths"].append((int(row["depth"]), int(row["value"]),
                                  int(row["best_enc"]), int(row["nodes"]), float(row["ms"])))
    for rec in pos.values():
        rec["depths"].sort()
    return pos


def convergence(depths, idx):
    """가장 깊은 depth 값과 같아지는, 그 값이 끝까지 유지되는 첫 depth(=수렴깊이).
       idx: 2=best_enc(move수렴), 1=value(값수렴)."""
    if not depths:
        return None
    final = depths[-1][idx]
    conv = depths[-1][0]
    for d, *rest in reversed(depths):           # 끝에서부터 final 과 같은 동안 내려감
        row = (d,) + tuple(rest)
        if row[idx] != final:
            break
        conv = d
    return conv


def derive(rec):
    d = rec["depths"]
    rec["final_value"] = d[-1][1]
    rec["final_depth"] = d[-1][0]
    rec["final_ms"] = d[-1][4]
    rec["final_nodes"] = d[-1][3]
    rec["conv_move"] = convergence(d, 2)
    rec["conv_val"] = convergence(d, 1)
    # converged = 값이 마지막 ≥3 depth 동안 안정(=deepest 값이 신뢰할 plateau).
    #   reached_max(maxDepth 도달=완전풀림)는 mid-nz 에선 거의 0이라, leaf오차(표C)엔 이 plateau 기준을 쓴다.
    rec["converged"] = (len(d) >= 3 and rec["conv_val"] <= rec["final_depth"] - 2)
    # depth→(value,ms,nodes) 인덱스 (gap/비용 조회용)
    rec["by_depth"] = {row[0]: row for row in d}


def bucket_key(nz, width):
    return int(round(nz / width) * width)


def main():
    ap = argparse.ArgumentParser(description="PROBE 오라클 CSV 집계")
    ap.add_argument("csv", help="probe.exe > probe.csv 의 경로")
    ap.add_argument("--bucket", type=int, default=10, help="nz 버킷 폭(기본 10)")
    args = ap.parse_args()

    pos = load_positions(args.csv)
    if not pos:
        sys.exit("빈 CSV — probe.exe 출력을 확인하라.")
    for rec in pos.values():
        derive(rec)

    buckets = defaultdict(list)
    for rec in pos.values():
        buckets[bucket_key(rec["nz"], args.bucket)].append(rec)
    keys = sorted(buckets)

    n_total = len(pos)
    n_solved = sum(r["reached_max"] for r in pos.values())
    print(f"# 포지션 {n_total}개  ·  캡 안 완전풀림 {n_solved} ({100.0*n_solved/n_total:.0f}%)  "
          f"·  버킷폭 nz±{args.bucket//2}\n")

    # ── A. 수렴/도달 ──
    print("## A. 수렴깊이 · 도달 (move수렴 = 둔 수가 안 바뀌는 첫 depth; 1순위 지표)")
    print(f"{'nz':>5} | {'n':>3} | {'solved%':>7} | {'medNlegal':>9} | "
          f"{'med_convMove':>12} | {'med_convVal':>11} | {'med_maxDepth':>12}")
    print("-" * 78)
    for k in keys:
        rs = buckets[k]
        sp = 100.0 * sum(r["reached_max"] for r in rs) / len(rs)
        print(f"{k:>5} | {len(rs):>3} | {sp:>6.0f}% | {fmt(med([r['nLegal'] for r in rs])):>9} | "
              f"{fmt(med([r['conv_move'] for r in rs])):>12} | "
              f"{fmt(med([r['conv_val'] for r in rs])):>11} | "
              f"{fmt(med([r['final_depth'] for r in rs])):>12}")

    # ── B. 완전풀림 비용 (solved only) ──
    print("\n## B. 완전풀림 비용 (solved 포지션만; nz↑ 지수증가 — '풀고 은행' 임계 탐색)")
    print(f"{'nz':>5} | {'n_solved':>8} | {'med_depth':>9} | {'med_ms':>8} | {'med_nodes':>12}")
    print("-" * 56)
    for k in keys:
        sv = [r for r in buckets[k] if r["reached_max"]]
        if not sv:
            print(f"{k:>5} | {0:>8} | {'-':>9} | {'-':>8} | {'-':>12}")
            continue
        print(f"{k:>5} | {len(sv):>8} | {fmt(med([r['final_depth'] for r in sv])):>9} | "
              f"{fmt(med([r['final_ms'] for r in sv])):>8} | "
              f"{fmt(med([r['final_nodes'] for r in sv])):>12}")

    # ── C. material-only 오차 vs depth (converged only) ──
    print("\n## C. material-only 오차 = |d-ply값 − deepest안정값| (칸), 값수렴 포지션만")
    hdr = "  ".join(f"d{d:>2}" for d in GAP_DEPTHS)
    print(f"{'nz':>5} | {'n':>3} | {hdr}")
    print("-" * (12 + 6 * len(GAP_DEPTHS)))
    for k in keys:
        sv = [r for r in buckets[k] if r["converged"]]
        if not sv:
            continue
        cells = []
        for d in GAP_DEPTHS:
            g = [abs(r["by_depth"][d][1] - r["final_value"]) / W_MAT
                 for r in sv if d in r["by_depth"]]
            cells.append(med(g))
        row = "  ".join((f"{c:>3.1f}" if c is not None else f"{'-':>3}") for c in cells)
        print(f"{k:>5} | {len(sv):>3} | {row}")

    print("\n해석: A의 med_convMove = 그 nz에서 '둔 수가 굳는' 깊이 → 예산은 거기 닿을 만큼.")
    print("      B에서 ms가 작아지는(=풀림 싼) nz 임계 이하 = '완전탐색+시간 은행' 구간.")
    print("      C에서 오차가 큰 깊이대 = material-only leaf 가 약함 → ③ 평가보강 후보.")


if __name__ == "__main__":
    main()

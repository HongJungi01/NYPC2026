#!/usr/bin/env python3
"""Conservative interactive bot.

This bot is intentionally minimal: it parses READY, responds OK, then submits
an empty legal command list every turn. It is useful as a safe integration
target while trained policies are not available.
"""

from __future__ import annotations

import sys


def readln() -> str:
    line = sys.stdin.readline()
    if not line:
        raise SystemExit(0)
    return line.rstrip("\n")


def main() -> None:
    first = readln().split()
    if not first or first[0] != "READY":
        raise SystemExit("expected READY")
    n, _k = (int(v) for v in readln().split())
    readln()
    readln()
    readln()
    for _ in range(n):
        readln()
    print("OK", flush=True)

    while True:
        line = readln()
        if line == "FINISH":
            return
        if not line.startswith("START"):
            raise SystemExit(f"expected START or FINISH, got {line}")
        sys.stdout.write("COMMAND\nTRAIN 0\nEND\n")
        sys.stdout.flush()

        line = readln()
        if line == "FINISH":
            return
        if not line.startswith("TURN"):
            raise SystemExit(f"expected TURN or FINISH, got {line}")
        readln()
        for section in ("UPGRADE", "TRAIN", "MOVE", "DAMAGE", "SIEGE"):
            header = readln().split()
            if not header or header[0] != section:
                raise SystemExit(f"expected {section}, got {' '.join(header)}")
            count = int(header[1])
            if section == "TRAIN" and count > 0:
                readln()
            else:
                for _ in range(count):
                    readln()
        end = readln()
        if end != "END":
            raise SystemExit(f"expected END, got {end}")


if __name__ == "__main__":
    main()

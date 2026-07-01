#!/usr/bin/env python3
# ============================================================
# NEXT NATION 사람 플레이어 프록시 (모드 C: 사람 vs AI)
#
# testing-tool 은 플레이어를 stdin/stdout 프로토콜로만 다룬다.
# 이 스크립트를 사람 측 EXEC 로 지정하면, testing-tool 이 보내는
# READY/START 를 받아 브리지 파일로 상태를 내보내고,
# 브라우저에서 구성한 명령(COMMAND...END)을 받아 stdout 으로 돌려준다.
#
# 통신 방식: 파일 기반(간단·견고). 브리지 폴더에
#   - turn_<T>.json   : 이번 턴에 사람이 봐야 할 정보 (프록시→브라우저)
#   - cmd_<T>.txt     : 사람이 제출한 명령 줄들 (브라우저→프록시)
# 를 주고받는다.
#
# 사용 예 (LEFT 가 사람):
#   python3 testing-tool.py --seed 42 -l log.txt \
#     -a "python3 human_player.py /tmp/nn_bridge LEFT" \
#     -b "python3 my_ai.py"
#
# 주의: 사람은 100ms 안에 둘 수 없다. 로컬 "연습용"으로만 사용하고,
#       시간제한을 늘리려면 testing-tool 의 SOFT_CAP_MS/토큰을 키운
#       사본을 쓰거나, 충분한 초읽기를 감안하라. (정식 채점과 무관)
# ============================================================
import sys, os, json, time

def main():
    if len(sys.argv) < 3:
        sys.stderr.write("usage: human_player.py <bridge_dir> <LEFT|RIGHT>\n")
        sys.exit(1)
    bridge = sys.argv[1]
    side = sys.argv[2]
    os.makedirs(bridge, exist_ok=True)

    def readln():
        l = sys.stdin.readline()
        if not l:
            sys.exit(0)
        return l.rstrip("\n")

    # READY 블록 수신 → 그대로 브리지에 맵 정보로 저장
    ready = readln()  # "READY LEFT|RIGHT"
    nk = readln()
    xs = readln()
    ys = readln()
    sh = readln()
    N = int(nk.split()[0])
    adj = [readln() for _ in range(N)]
    with open(os.path.join(bridge, "map.json"), "w", encoding="utf-8") as f:
        json.dump({"ready": ready, "nk": nk, "x": xs, "y": ys,
                   "strongholds": sh, "adj": adj, "side": side}, f, ensure_ascii=False)
    print("OK", flush=True)

    while True:
        line = readln()
        if line == "FINISH":
            # 종료 신호를 브리지에 남기고 정상 종료
            with open(os.path.join(bridge, "finish.flag"), "w") as f:
                f.write("1")
            return
        parts = line.split()
        if not parts or parts[0] != "START":
            continue
        turn = int(parts[2])

        # 사람 차례 알림 파일 작성
        req = os.path.join(bridge, f"turn_{turn}.json")
        with open(req, "w", encoding="utf-8") as f:
            json.dump({"turn": turn, "side": side, "waiting": True}, f)

        # 브라우저가 명령을 제출할 때까지 대기 (cmd_<T>.txt 출현)
        cmd_path = os.path.join(bridge, f"cmd_{turn}.txt")
        while not os.path.exists(cmd_path):
            if os.path.exists(os.path.join(bridge, "finish.flag")):
                return
            time.sleep(0.03)
        with open(cmd_path, "r", encoding="utf-8") as f:
            body = f.read()
        try:
            os.remove(req)
        except OSError:
            pass

        # 사람이 구성한 명령을 testing-tool 로 전달
        sys.stdout.write("COMMAND\n")
        for ln in body.splitlines():
            ln = ln.strip()
            if ln and ln not in ("COMMAND", "END"):
                sys.stdout.write(ln + "\n")
        sys.stdout.write("END\n")
        sys.stdout.flush()

        # 턴 결과를 읽어 브리지에 저장(브라우저가 화면 갱신용으로 사용)
        res = []
        first = readln()
        if first == "FINISH":
            with open(os.path.join(bridge, "finish.flag"), "w") as f:
                f.write("1")
            return
        res.append(first)  # "TURN T"
        # END 까지 읽기
        while True:
            l = readln()
            res.append(l)
            if l.startswith("END"):
                break
        with open(os.path.join(bridge, f"result_{turn}.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(res))


if __name__ == "__main__":
    main()

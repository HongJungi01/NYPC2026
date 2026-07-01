#!/usr/bin/env python3
# ============================================================
# NEXT NATION 뷰어 브리지 서버
#   - 정적 파일(viewer.html 등) 서빙
#   - "새 게임" 요청 시 testing-tool.py 를 seed/랜덤으로 실행
#   - 로그를 실시간 tail 하여 SSE 로 브라우저에 push (실시간 관전)
#   - 사람 vs AI 직접 플레이 중계 (human_player.py 와 연동)
#
# 의존성: 표준 라이브러리만 사용 (설치 불필요)
# 실행:  python3 viewer_server.py
#        브라우저에서 http://localhost:8770/viewer.html
# ============================================================
import http.server, socketserver, threading, subprocess, os, sys, json, time, queue, urllib.parse, random, shlex, webbrowser, glob, shutil

PORT = 8770
ROOT = os.path.dirname(os.path.abspath(__file__))

def find_existing(paths):
    for path in paths:
        if path and os.path.exists(path):
            return path
    return None

def find_sample_code():
    parent = os.path.dirname(ROOT)
    return find_existing([
        os.environ.get("NN_SAMPLE_CODE"),
        os.path.join(ROOT, "sample-code.py"),
        os.path.join(parent, "sample-code.py"),
        os.path.join(parent, "nation-providing", "sample-code.py"),
    ])

def python_player_cmd(path, arg):
    if path:
        return f"python3 {shlex.quote(path)} {arg}"
    return f"python3 sample-code.py {arg}"

# testing-tool.py 위치: 같은 폴더에 없으면 nation-providing/ 에서 탐색
TOOL = os.environ.get("NN_TOOL") or find_existing([
    os.path.join(ROOT, "testing-tool.py"),
    os.path.join(ROOT, "testing-tool-practice.py"),
    os.path.join(os.path.dirname(ROOT), "nation-providing", "testing-tool.py"),
])
SAMPLE_CODE = find_sample_code()
DEFAULT_WORKDIR = os.environ.get("NN_WORKDIR") or (os.path.dirname(SAMPLE_CODE) if SAMPLE_CODE else ROOT)
DEFAULT_EXEC1 = os.environ.get("NN_EXEC1") or python_player_cmd(SAMPLE_CODE, "P1")
DEFAULT_EXEC2 = os.environ.get("NN_EXEC2") or python_player_cmd(SAMPLE_CODE, "P2")

# --- 실시간 게임 상태 ---
class Live:
    def __init__(self):
        self.subscribers = []
        self.lock = threading.Lock()
        self.proc = None
        self.logpath = None
        self.running = False
        self.seed = None
LIVE = Live()

# --- 사람 vs AI 직접 플레이 상태 ---
class Human:
    def __init__(self):
        self.bridge = None
        self.side = None
        self.proc = None
        self.running = False
        self.logpath = None
        self.errpath = None
        self.seed = None
HUMAN = Human()

def broadcast(event, data):
    msg = f"event: {event}\ndata: {json.dumps(data, ensure_ascii=False)}\n\n"
    with LIVE.lock:
        dead = []
        for q in LIVE.subscribers:
            try: q.put_nowait(msg)
            except Exception: dead.append(q)
        for q in dead:
            LIVE.subscribers.remove(q)

def tail_log(path):
    for _ in range(200):
        if os.path.exists(path): break
        time.sleep(0.02)
    buf = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        while LIVE.running:
            line = f.readline()
            if not line:
                time.sleep(0.05); continue
            buf.append(line)
            s = line.strip()
            if s.startswith("END MAP"):
                broadcast("map", {"text": "".join(buf)}); buf = []
            elif s.startswith("END TURN"):
                broadcast("turn", {"text": "".join(buf)}); buf = []
            elif s.startswith("RESULT "):
                broadcast("result", {"text": s}); buf = []; break
    broadcast("done", {})

SKIP_EXTS = {'.py', '.js', '.html', '.css', '.md', '.txt', '.log',
             '.ini', '.json', '.zip', '.png', '.jpg', '.jpeg', '.gif',
             '.command', '.sh', '.bat', '.DS_Store', '.map', '.gz', '.cpp', '.c', '.h'}

BOT_DIR = os.path.join(os.path.dirname(ROOT), "BOT")

def check_binary_platform(path):
    """Returns (ok, error_msg). Reads magic bytes to detect OS platform mismatch."""
    try:
        with open(path, 'rb') as f:
            magic = f.read(4)
    except Exception:
        return True, None
    is_win   = magic[:2] == b'MZ'
    is_elf   = magic[:4] == b'\x7fELF'
    is_macho = magic[:4] in (b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
                              b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe')
    plat = sys.platform  # 'win32' / 'darwin' / 'linux'
    if is_win and plat != 'win32':
        return False, f"Windows 실행파일(.exe)은 이 운영체제({plat})에서 실행할 수 없습니다"
    if is_elf and plat != 'linux':
        return False, f"Linux 실행파일은 이 운영체제({plat})에서 실행할 수 없습니다"
    if is_macho and plat != 'darwin':
        return False, f"macOS 실행파일은 이 운영체제({plat})에서 실행할 수 없습니다"
    return True, None


def list_binaries():
    """BOT 폴더에서 실행 가능한 파일 목록을 반환한다."""
    result = []
    try:
        for name in sorted(os.listdir(BOT_DIR)):
            if name.startswith('.'): continue
            path = os.path.join(BOT_DIR, name)
            if not os.path.isfile(path): continue
            ext = os.path.splitext(name)[1].lower()
            if ext in SKIP_EXTS: continue
            if os.access(path, os.X_OK) or ext == '.exe':
                result.append({"name": name, "path": path})
    except Exception:
        pass
    return result

def start_game(seed, exec1, exec2):
    if LIVE.running and LIVE.proc and LIVE.proc.poll() is None:
        try: LIVE.proc.terminate()
        except Exception: pass
    ts = int(time.time()*1000)
    logpath = os.path.join(ROOT, f"_live_{ts}.log")
    cmd = ["python3", TOOL, "-l", logpath, "-a", exec1, "-b", exec2]
    if seed is None:
        seed = random.randint(0, (1<<63)-1)
    cmd += ["--seed", str(seed)]
    LIVE.seed = seed
    LIVE.logpath = logpath
    LIVE.running = True
    LIVE.proc = subprocess.Popen(cmd, cwd=DEFAULT_WORKDIR,
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    threading.Thread(target=tail_log, args=(logpath,), daemon=True).start()
    return seed

def start_human_game(seed, human_side, ai_exec):
    if HUMAN.proc and HUMAN.proc.poll() is None:
        try: HUMAN.proc.terminate()
        except Exception: pass
    ts = int(time.time()*1000)
    bridge = os.path.join(ROOT, f"_human_{ts}")
    os.makedirs(bridge, exist_ok=True)
    logpath = os.path.join(ROOT, f"_humanlog_{ts}.log")

    practice = os.path.join(ROOT, "testing-tool-practice.py")
    tool = practice if os.path.exists(practice) else TOOL
    human_proxy = os.path.join(ROOT, "human_player.py")
    human_cmd = f'python3 "{human_proxy}" "{bridge}" {human_side}'

    if human_side == "LEFT":
        exec1, exec2 = human_cmd, ai_exec
    else:
        exec1, exec2 = ai_exec, human_cmd

    if seed is None:
        seed = random.randint(0, (1<<63)-1)
    cmd = ["python3", tool, "-l", logpath, "-a", exec1, "-b", exec2, "--seed", str(seed)]

    HUMAN.bridge = bridge
    HUMAN.side = human_side
    HUMAN.logpath = logpath
    HUMAN.errpath = os.path.join(ROOT, f"_humanerr_{ts}.txt")
    HUMAN.seed = seed
    HUMAN.running = True
    err = open(HUMAN.errpath, "w", encoding="utf-8")
    HUMAN.proc = subprocess.Popen(cmd, cwd=DEFAULT_WORKDIR,
                                  stdout=subprocess.DEVNULL, stderr=err)
    return seed

def human_state():
    if not HUMAN.bridge:
        return {"running": False}
    b = HUMAN.bridge
    out = {"running": HUMAN.running, "side": HUMAN.side, "seed": HUMAN.seed}
    mp = os.path.join(b, "map.json")
    if os.path.exists(mp):
        try:
            with open(mp, encoding="utf-8") as f: out["map"] = json.load(f)
        except Exception: pass
    if os.path.exists(os.path.join(b, "finish.flag")):
        out["finished"] = True
        HUMAN.running = False
    if HUMAN.proc and HUMAN.proc.poll() is not None:
        HUMAN.running = False
        out["running"] = False
        out["exit_code"] = HUMAN.proc.returncode
        if not out.get("finished"):
            out["error"] = "human game process exited"
            if HUMAN.errpath and os.path.exists(HUMAN.errpath):
                try:
                    with open(HUMAN.errpath, encoding="utf-8", errors="replace") as f:
                        tail = f.read()[-2000:].strip()
                    if tail: out["stderr"] = tail
                except Exception: pass
    waiting = None
    for fn in os.listdir(b):
        if fn.startswith("turn_") and fn.endswith(".json"):
            try: t = int(fn[5:-5])
            except Exception: continue
            if waiting is None or t > waiting: waiting = t
    out["waiting_turn"] = waiting
    if HUMAN.logpath and os.path.exists(HUMAN.logpath):
        try:
            with open(HUMAN.logpath, encoding="utf-8", errors="replace") as f:
                out["log"] = f.read()
        except Exception: pass
    return out

def cleanup_logs():
    keep = set()
    if LIVE.logpath: keep.add(os.path.abspath(LIVE.logpath))
    if HUMAN.logpath: keep.add(os.path.abspath(HUMAN.logpath))
    if HUMAN.errpath: keep.add(os.path.abspath(HUMAN.errpath))
    if HUMAN.bridge: keep.add(os.path.abspath(HUMAN.bridge))

    removed_files, removed_dirs = 0, 0
    for pattern in ("_live_*.log", "_humanlog_*.log", "_humanerr_*.txt"):
        for f in glob.glob(os.path.join(ROOT, pattern)):
            if os.path.abspath(f) not in keep:
                try: os.remove(f); removed_files += 1
                except Exception: pass
    for d in glob.glob(os.path.join(ROOT, "_human_*")):
        if os.path.isdir(d) and os.path.abspath(d) not in keep:
            try: shutil.rmtree(d); removed_dirs += 1
            except Exception: pass
    return {"ok": True, "removed_files": removed_files, "removed_dirs": removed_dirs}

def human_submit(turn, lines):
    if not HUMAN.bridge:
        return {"ok": False, "error": "no active human game"}
    cmd_path = os.path.join(HUMAN.bridge, f"cmd_{turn}.txt")
    with open(cmd_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return {"ok": True}

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)
    def log_message(self, *a): pass

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/events":
            return self.handle_sse()
        if parsed.path == "/status":
            return self.send_json({"running": LIVE.running, "seed": LIVE.seed})
        if parsed.path == "/list-binaries":
            return self.send_json(list_binaries())
        if parsed.path == "/human/state":
            return self.send_json(human_state())
        # 규칙 파일: engine 폴더 밖의 실제 규칙 문서를 서빙
        if parsed.path == "/NEXT_NATION_게임규칙.md":
            parent = os.path.dirname(ROOT)
            candidates = [
                os.path.join(parent, "NEXT_NATION_규칙(태훈_수정).md"),
                os.path.join(parent, "NEXT_NATION_게임규칙.md"),
                os.path.join(ROOT, "NEXT_NATION_게임규칙.md"),
            ]
            for rules_path in candidates:
                if os.path.exists(rules_path):
                    try:
                        with open(rules_path, encoding="utf-8") as f:
                            content = f.read().encode("utf-8")
                        self.send_response(200)
                        self.send_header("Content-Type", "text/plain; charset=utf-8")
                        self.send_header("Content-Length", str(len(content)))
                        self.send_header("Access-Control-Allow-Origin", "*")
                        self.end_headers()
                        self.wfile.write(content)
                    except Exception:
                        self.send_error(500)
                    return
            self.send_error(404)
            return
        return super().do_GET()

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/new":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b"{}"
            try: opts = json.loads(body or b"{}")
            except Exception: opts = {}
            seed = opts.get("seed", None)
            if seed in ("", None): seed = None
            else:
                try: seed = int(seed)
                except Exception: seed = None
            exec1 = opts.get("exec1") or DEFAULT_EXEC1
            exec2 = opts.get("exec2") or DEFAULT_EXEC2
            used = start_game(seed, exec1, exec2)
            return self.send_json({"ok": True, "seed": used})
        if parsed.path == "/human/new":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b"{}"
            try: opts = json.loads(body or b"{}")
            except Exception: opts = {}
            seed = opts.get("seed", None)
            if seed in ("", None): seed = None
            else:
                try: seed = int(seed)
                except Exception: seed = None
            side = (opts.get("side") or "LEFT").upper()
            if side not in ("LEFT", "RIGHT"): side = "LEFT"
            ai_raw = opts.get("ai")
            if ai_raw:
                ok, err = check_binary_platform(ai_raw)
                if not ok:
                    return self.send_json({"ok": False, "error": err})
                # Windows cmd.exe는 단일따옴표를 경로 구분자로 인식하지 않으므로 이중따옴표 사용
                ai_exec = f'"{ai_raw}"' if sys.platform == 'win32' else shlex.quote(ai_raw)
            else:
                ai_exec = DEFAULT_EXEC2
            used = start_human_game(seed, side, ai_exec)
            return self.send_json({"ok": True, "seed": used, "side": side})
        if parsed.path == "/human/submit":
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length) if length else b"{}"
            try: opts = json.loads(body or b"{}")
            except Exception: opts = {}
            turn = opts.get("turn")
            lines = opts.get("lines", [])
            if turn is None:
                return self.send_json({"ok": False, "error": "no turn"})
            return self.send_json(human_submit(int(turn), lines))
        if parsed.path == "/cleanup":
            return self.send_json(cleanup_logs())
        self.send_error(404)

    def handle_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "keep-alive")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        q = queue.Queue(maxsize=1000)
        with LIVE.lock:
            LIVE.subscribers.append(q)
        try:
            self.wfile.write(b": connected\n\n"); self.wfile.flush()
            while True:
                try:
                    msg = q.get(timeout=15)
                except queue.Empty:
                    self.wfile.write(b": ping\n\n"); self.wfile.flush(); continue
                self.wfile.write(msg.encode("utf-8")); self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        finally:
            with LIVE.lock:
                if q in LIVE.subscribers: LIVE.subscribers.remove(q)

    def send_json(self, obj):
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(data)

class ThreadingServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

def main():
    global PORT
    if len(sys.argv) > 1:
        for arg in sys.argv[1:]:
            if arg.startswith("--"): continue
            try: PORT = int(arg)
            except Exception: pass
    if not TOOL or not os.path.exists(TOOL):
        print(f"[경고] testing-tool.py 를 찾지 못함: {TOOL}")
        print("       NN_TOOL 환경변수로 경로를 지정하거나, nation-providing 폴더를 확인하세요.")
    with ThreadingServer(("", PORT), Handler) as httpd:
        url = f"http://localhost:{PORT}/viewer.html"
        print(f"NEXT NATION 뷰어 서버 실행 중")
        print(f"  → {url}")
        print(f"  testing-tool: {TOOL}")
        print(f"  workdir: {DEFAULT_WORKDIR}")
        print(f"  sample-code: {SAMPLE_CODE or '(찾지 못함)'}")
        print(f"  종료: Ctrl+C")
        if "--open" in sys.argv:
            threading.Timer(0.5, lambda: webbrowser.open(url)).start()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n로그 정리 중...")
            result = cleanup_logs()
            print(f"  삭제된 파일: {result['removed_files']}개, 폴더: {result['removed_dirs']}개")
            print("종료합니다.")

if __name__ == "__main__":
    main()

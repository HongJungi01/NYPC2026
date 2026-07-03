"""Interactive adapters for compiled C++ manual bots."""

from __future__ import annotations

import queue
import subprocess
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import TextIO

from nextnation_ai.game.actions import CommandAction, MoveCommand
from nextnation_ai.game.legal import validate_action
from nextnation_ai.game.map import GameMap
from nextnation_ai.game.simulator import DayEvents
from nextnation_ai.game.state import GameState, Side, UnitId


@dataclass
class ManualCppAgent:
    path: Path
    name: str | None = None
    handshake_timeout: float = 1.0
    command_timeout: float = 0.5
    process: subprocess.Popen | None = field(default=None, init=False, repr=False)
    _reads: queue.Queue[str | None] = field(default_factory=queue.Queue, init=False, repr=False)
    _stdout_thread: threading.Thread | None = field(default=None, init=False, repr=False)
    _stderr_thread: threading.Thread | None = field(default=None, init=False, repr=False)
    side: Side | None = field(default=None, init=False)

    def __post_init__(self) -> None:
        self.path = Path(self.path).resolve()
        if self.name is None:
            self.name = self.path.stem

    def _stdout_loop(self, stream: TextIO) -> None:
        try:
            while True:
                line = stream.readline()
                if not line:
                    break
                self._reads.put(line.rstrip("\n"))
        finally:
            self._reads.put(None)

    def _stderr_loop(self, stream: TextIO) -> None:
        try:
            for _line in stream:
                pass
        except Exception:
            pass

    def _send(self, line: str) -> None:
        if self.process is None or self.process.stdin is None:
            raise RuntimeError("manual C++ process is not running")
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def _readline(self, timeout: float) -> str | None:
        try:
            line = self._reads.get(timeout=timeout)
        except queue.Empty:
            return None
        return line

    def start_episode(self, game_map: GameMap, side: Side) -> None:
        self.close()
        self.side = side
        self._reads = queue.Queue()
        self.process = subprocess.Popen(
            [str(self.path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            cwd=str(self.path.parents[1]) if self.path.parent.name == "manual_bins" else None,
        )
        assert self.process.stdout is not None
        assert self.process.stderr is not None
        self._stdout_thread = threading.Thread(target=self._stdout_loop, args=(self.process.stdout,), daemon=True)
        self._stderr_thread = threading.Thread(target=self._stderr_loop, args=(self.process.stderr,), daemon=True)
        self._stdout_thread.start()
        self._stderr_thread.start()
        self._send_ready(game_map, side)
        if self._readline(self.handshake_timeout) != "OK":
            self.close()
            raise RuntimeError(f"manual C++ bot did not handshake: {self.name}")

    def close(self) -> None:
        proc = self.process
        self.process = None
        self.side = None
        if proc is None:
            return
        try:
            if proc.stdin is not None:
                proc.stdin.close()
        except Exception:
            pass
        try:
            proc.wait(timeout=0.2)
        except subprocess.TimeoutExpired:
            proc.terminate()
            try:
                proc.wait(timeout=0.2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()

    def _send_ready(self, game_map: GameMap, side: Side) -> None:
        self._send(f"READY {side.word}")
        self._send(f"{game_map.N} {game_map.K}")
        self._send(" ".join(str(v) for v in game_map.x))
        self._send(" ".join(str(v) for v in game_map.y))
        self._send(" ".join(str(v) for v in game_map.points))
        for row in game_map.adj:
            self._send(" ".join(str(v) for v in [len(row), *row]))

    def _sanitize(self, state: GameState, player: Side, action: CommandAction) -> CommandAction:
        clean = CommandAction()
        for region in action.upgrades:
            proposal = CommandAction(clean.train_n, [*clean.upgrades, region], list(clean.moves))
            if validate_action(state, player, proposal):
                clean = proposal
        for move in action.moves:
            proposal = CommandAction(clean.train_n, list(clean.upgrades), [*clean.moves, move])
            if validate_action(state, player, proposal):
                clean = proposal
        if action.train_n > 0:
            proposal = CommandAction(action.train_n, list(clean.upgrades), list(clean.moves))
            if validate_action(state, player, proposal):
                clean = proposal
        return clean

    def act(self, state: GameState, player: Side) -> CommandAction:
        if self.process is None or self.side is not player:
            return CommandAction()
        self._send(f"START TURN {state.day + 1}")
        first = self._readline(self.command_timeout)
        if first != "COMMAND":
            return CommandAction()
        upgrades: list[int] = []
        moves: list[MoveCommand] = []
        train_n = 0
        while True:
            line = self._readline(self.command_timeout)
            if line is None or line == "END":
                break
            parts = line.split()
            if not parts:
                continue
            try:
                if parts[0] == "UPGRADE" and len(parts) == 2:
                    upgrades.append(int(parts[1]))
                elif parts[0] == "MOVE" and len(parts) == 3:
                    uid = UnitId.parse(parts[1])
                    if uid.owner is player:
                        moves.append(MoveCommand(uid, int(parts[2])))
                elif parts[0] == "TRAIN" and len(parts) == 2:
                    train_n = max(0, int(parts[1]))
            except Exception:
                continue
        action = CommandAction(train_n, upgrades, moves)
        if validate_action(state, player, action):
            return action
        return self._sanitize(state, player, action)

    def observe(self, state: GameState, player: Side, events: DayEvents, *, self_time: int = 0, opp_time: int = 0) -> None:
        if self.process is None or self.side is not player:
            return
        self._send(f"TURN {state.day}")
        self._send(
            f"TIME {self_time} {state.player(player).remaining_time_tokens} "
            f"{opp_time} {state.player(player.opponent).remaining_time_tokens}"
        )
        self._send(f"UPGRADE {len(events.upgrades)}")
        for region, side in events.upgrades:
            self._send(f"{side.letter} {region}")
        self._send(f"TRAIN {len(events.trained)}")
        if events.trained:
            self._send(" ".join(str(uid) for uid in events.trained))
        self._send(f"MOVE {len(events.moves)}")
        for uid, region in events.moves:
            self._send(f"{uid} {region}")
        self._send(f"DAMAGE {len(events.damages)}")
        for cause, uid, damage in events.damages:
            self._send(f"{cause} {uid} {damage}")
        self._send(f"SIEGE {len(events.sieges)}")
        for side, region, damage in events.sieges:
            self._send(f"{side.letter} {region} {damage}")
        self._send("END")

    def finish(self) -> None:
        if self.process is not None:
            try:
                self._send("FINISH")
            except Exception:
                pass
        self.close()

"""Battlefield graph representation and weighted movement preprocessing."""

from __future__ import annotations

import heapq
import math
from dataclasses import dataclass, field
from typing import Iterable, Sequence


INF_DISTANCE = 10**30


def canonical_node(node: int, player: int, n: int) -> int:
    """Return node id in the player's canonical LEFT-facing perspective."""
    return node if int(player) == 0 else n - 1 - node


def absolute_node(node: int, player: int, n: int) -> int:
    """Inverse of canonical_node."""
    return canonical_node(node, player, n)


@dataclass
class GameMap:
    N: int
    K: int
    x: list[int]
    y: list[int]
    points: list[int]
    adj: list[list[int]]
    dist: list[list[int]] = field(default_factory=list)
    next_hop: list[list[int | None]] = field(default_factory=list)

    def __post_init__(self) -> None:
        if self.N != len(self.x) or self.N != len(self.y) or self.N != len(self.adj):
            raise ValueError("map dimensions do not match N")
        self.points = sorted(int(p) for p in self.points)
        self.adj = [sorted(set(int(v) for v in row)) for row in self.adj]
        if not self.dist or not self.next_hop:
            self.precompute_shortest_paths()

    def __deepcopy__(self, memo) -> "GameMap":
        # The map is immutable after __post_init__ (nothing may mutate it —
        # canonicalize() returns a new instance). Sharing it lets
        # GameState.clone() skip copying the N^2 dist/next_hop tables, which
        # dominated the ~2.7 ms/clone cost at N=109.
        return self

    @classmethod
    def from_lines(cls, lines: Sequence[str]) -> "GameMap":
        rows = [line.strip() for line in lines if line.strip()]
        if len(rows) < 4:
            raise ValueError("map input needs at least 4 lines")
        n, k = (int(v) for v in rows[0].split())
        x = [int(v) for v in rows[1].split()]
        y = [int(v) for v in rows[2].split()]
        points = [int(v) for v in rows[3].split()]
        if len(points) != k:
            raise ValueError("stronghold count does not match K")
        if len(rows) < 4 + n:
            raise ValueError("map input is missing adjacency rows")
        adj: list[list[int]] = []
        for i in range(n):
            parts = [int(v) for v in rows[4 + i].split()]
            if not parts:
                raise ValueError(f"empty adjacency row for node {i}")
            degree, neigh = parts[0], parts[1:]
            if degree != len(neigh):
                raise ValueError(f"bad degree on adjacency row {i}")
            adj.append(neigh)
        return cls(n, k, x, y, points, adj)

    @classmethod
    def from_text(cls, text: str) -> "GameMap":
        return cls.from_lines(text.splitlines())

    def to_lines(self) -> list[str]:
        rows = [
            f"{self.N} {self.K}",
            " ".join(str(v) for v in self.x),
            " ".join(str(v) for v in self.y),
            " ".join(str(v) for v in self.points),
        ]
        rows.extend(" ".join(str(v) for v in [len(row), *row]) for row in self.adj)
        return rows

    def edge_length(self, u: int, v: int) -> int:
        dx = self.x[u] - self.x[v]
        dy = self.y[u] - self.y[v]
        return math.ceil(math.sqrt(dx * dx + dy * dy))

    def dijkstra_to(self, target: int) -> list[int]:
        dist = [INF_DISTANCE] * self.N
        dist[target] = 0
        heap: list[tuple[int, int]] = [(0, target)]
        while heap:
            du, u = heapq.heappop(heap)
            if du != dist[u]:
                continue
            for v in self.adj[u]:
                cand = du + self.edge_length(u, v)
                if cand < dist[v]:
                    dist[v] = cand
                    heapq.heappush(heap, (cand, v))
        return dist

    def precompute_shortest_paths(self) -> None:
        dist = [[INF_DISTANCE] * self.N for _ in range(self.N)]
        next_hop: list[list[int | None]] = [[None] * self.N for _ in range(self.N)]

        for target in range(self.N):
            to_target = self.dijkstra_to(target)
            for u in range(self.N):
                dist[u][target] = to_target[u]
                if u == target or to_target[u] >= INF_DISTANCE:
                    continue
                best_v: int | None = None
                best_score = INF_DISTANCE
                for v in self.adj[u]:
                    if to_target[v] >= INF_DISTANCE:
                        continue
                    score = self.edge_length(u, v) + to_target[v]
                    if score < best_score or (score == best_score and (best_v is None or v < best_v)):
                        best_score = score
                        best_v = v
                next_hop[u][target] = best_v

        self.dist = dist
        self.next_hop = next_hop

    def is_reachable(self, src: int, dst: int) -> bool:
        return 0 <= src < self.N and 0 <= dst < self.N and self.dist[src][dst] < INF_DISTANCE

    def next_step(self, src: int, dst: int) -> int | None:
        if src == dst:
            return None
        return self.next_hop[src][dst]

    def hq_of(self, player: int) -> int:
        return 0 if int(player) == 0 else self.N - 1

    def canonicalize(self, player: int) -> "GameMap":
        if int(player) == 0:
            return GameMap(self.N, self.K, list(self.x), list(self.y), list(self.points), [list(a) for a in self.adj])

        n = self.N
        x = [0] * n
        y = [0] * n
        adj: list[list[int]] = [[] for _ in range(n)]
        for absolute in range(n):
            c = canonical_node(absolute, player, n)
            x[c] = -self.x[absolute]
            y[c] = -self.y[absolute]
            adj[c] = sorted(canonical_node(v, player, n) for v in self.adj[absolute])
        points = sorted(canonical_node(p, player, n) for p in self.points)
        return GameMap(n, self.K, x, y, points, adj)


def make_line_map(n: int = 51, k: int | None = None) -> GameMap:
    """Create a deterministic symmetric connected map useful for smoke tests."""
    if n % 2 != 1:
        raise ValueError("n must be odd")
    if k is None:
        k = max(1, math.ceil(0.15 * n))
        if k % 2 == 0:
            k += 1
    if k % 2 != 1:
        raise ValueError("k must be odd")
    if k >= n - 1:
        raise ValueError("k leaves no room for HQ-only endpoints")

    mid = n // 2
    x = [i - mid for i in range(n)]
    y = [0 for _ in range(n)]
    adj: list[list[int]] = []
    for i in range(n):
        row: list[int] = []
        if i > 0:
            row.append(i - 1)
        if i + 1 < n:
            row.append(i + 1)
        adj.append(row)

    points = {mid}
    offset = 1
    while len(points) < k:
        if mid - offset <= 0 or mid + offset >= n - 1:
            raise ValueError("cannot place requested strongholds")
        points.add(mid - offset)
        points.add(mid + offset)
        offset += 1
    return GameMap(n, k, x, y, sorted(points), adj)


def parse_map_file(path: str) -> GameMap:
    with open(path, "r", encoding="utf-8") as f:
        return GameMap.from_lines(f.readlines())

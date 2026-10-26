"""Where can rabbit-holes' commitment fire? Static geometry of the official pool, no games.

    python bots/specialists/tools/dash-edges.py

Replicates rabbit-holes/src/draft-plan.h: the first 32 straight cardinal 2-step pairs (ascending
tile order) whose ends are walkable public terrain and whose midpoint is a hole, ranked by holes
within Chebyshev 2 of the midpoint, then endpoint indices. Then core.h's acceptance: walking
separation between the ends of at least profile::minimum_walk_separation (6), or unreachable,
over public terrain (walls, holes, altars, enemy spawn region). Whether a route then saves the
required 4 steps depends on live targets and is not modelled. Re-check this file against the bot
sources after editing them.
"""
from __future__ import annotations

import json
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CANDIDATES, SEPARATION, FIELD_CAP = 32, 6, 256


def drafted_edge(m: dict, side: str) -> tuple[int, int] | None:
    w, h, t = m["width"], m["height"], m["terrain"]
    holes, walls = set(t.get("hole", [])), set(t.get("wall", []))
    e = m["spawns"]["teamB" if side == "teamA" else "teamA"]  # spawn rectangles are [min, max)
    enemy = {y * w + x for y in range(e["minY"], e["maxY"]) for x in range(e["minX"], e["maxX"])}
    blocked = walls | holes | set(t.get("altar", [])) | enemy
    best, seen = None, 0
    for a in range(w * h):
        x, y = a % w, a // w
        for bx, by in ((x + 2, y), (x, y + 2)):
            if seen >= CANDIDATES:
                return (best[0], best[1]) if best else None
            mx, my = (x + bx) // 2, (y + by) // 2
            b, mid = by * w + bx, my * w + mx
            if bx >= w or by >= h or a in blocked or b in blocked or mid not in holes or mid in walls:
                continue
            seen += 1
            n = sum(1 for yy in range(my - 2, my + 3) for xx in range(mx - 2, mx + 3)
                    if 0 <= xx < w and 0 <= yy < h and yy * w + xx in holes)
            if best is None or (n, -a, -b) > (best[2], -best[0], -best[1]):
                best = (a, b, n)
    return (best[0], best[1]) if best else None


def separation(m: dict, side: str, a: int, b: int) -> int | str:
    """8-neighbour walking distance over public terrain, 'unreachable' when the field completes
    without reaching b, or 'unknown' when the field hits its expansion cap first."""
    w, h, t = m["width"], m["height"], m["terrain"]
    e = m["spawns"]["teamB" if side == "teamA" else "teamA"]
    blocked = set(t.get("wall", [])) | set(t.get("hole", [])) | set(t.get("altar", []))
    blocked |= {y * w + x for y in range(e["minY"], e["maxY"]) for x in range(e["minX"], e["maxX"])}
    dist, queue, capped = {a: 0}, deque([a]), False
    while queue:
        c = queue.popleft()
        if c == b:
            return dist[c]
        if dist[c] >= FIELD_CAP:
            capped = True
            continue
        cx, cy = c % w, c // w
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                nx, ny = cx + dx, cy + dy
                n = ny * w + nx
                if (dx or dy) and 0 <= nx < w and 0 <= ny < h and n not in blocked and n not in dist:
                    dist[n] = dist[c] + 1
                    queue.append(n)
    return "unknown" if capped else "unreachable"


def main() -> int:
    maps = json.loads((ROOT / "maps" / "geometry.json").read_text(encoding="utf-8"))["maps"]
    usable = 0
    for m in maps:
        cells = []
        for side in ("teamA", "teamB"):
            edge = drafted_edge(m, side)
            if edge is None:
                cells.append("no edge")
                continue
            sep = separation(m, side, *edge)
            ok = sep == "unreachable" or (isinstance(sep, int) and sep >= SEPARATION)
            cells.append(f"{edge[0]}->{edge[1]} walk {sep} {'accepted' if ok else 'rejected'}")
        if any("accepted" in c for c in cells):
            usable += 1
        if any(c != "no edge" for c in cells):
            print(f"{m['id']:<24} A: {cells[0]:<32} B: {cells[1]}")
    print(f"edge accepted on {usable}/{len(maps)} pool maps; elsewhere Vendetta walks like B0")
    return 0


if __name__ == "__main__":
    sys.exit(main())

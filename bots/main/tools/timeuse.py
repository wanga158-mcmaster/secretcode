"""Aggregate bots/main time-allocation lines ("t r<round> k<type> pvp.. idle..") from replays.

Usage: python -B bots/main/tools/timeuse.py PATH ... [--by-type] [--side a|b] [--bot SEL]
PATH: local replays or run directories, online downloads under results/online/ (see replays.py).
Uses each champion's latest line per game (time-use and idle-cause lines are logged every 500
rounds in release builds). Also counts stalled games: a champion idle for more than 20% of its
logged turns (the 2026-10-21 fuel stall beside an adjacent enemy spawn idled 92%). A stall is a
release blocker; each one found gets a fixture in tests/run.py.
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replays  # noqa: E402

LINE = re.compile(r"t r(\d+) k(\d+) pvp(\d+) hp(\d+) trib(\d+) cap(\d+) fight(\d+) stage(\d+) trav(\d+) idle(\d+)(?: await(\d+))?")
KEYS = ["pvp", "hp", "trib", "cap", "fight", "stage", "trav", "idle", "await"]
IDLE = re.compile(r"i r(\d+) k(\d+) fuel(\d+) none(\d+) resp(\d+) stall(\d+) blocked(\d+) other(\d+)")
IDLE_KEYS = ["fuel", "none", "resp", "stall", "blocked", "other"]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    replays.add_arguments(p)
    p.add_argument("--by-type", action="store_true", help="one row per champion type")
    args = p.parse_args()
    totals = defaultdict(lambda: [0] * 9)
    idle_totals = [0] * 6
    games = stalled = 0
    for game in replays.games(args.paths, args.side, args.bot):
        text = game.text
        games += 1
        latest = {}
        for m in LINE.finditer(text):
            r, k = int(m.group(1)), int(m.group(2))
            if k not in latest or r >= latest[k][0]:
                latest[k] = (r, [int(m.group(i) or 0) for i in range(3, 12)])
        if any(sum(v) and v[7] > 0.2 * sum(v) for _, v in latest.values()):
            stalled += 1
            print(f"stalled: {game.source} side {game.side} map {game.map}")
        for k, (_, v) in latest.items():
            key = k if args.by_type else "all"
            totals[key] = [a + b for a, b in zip(totals[key], v)]
        idle_latest = {}
        for m in IDLE.finditer(text):
            r, k = int(m.group(1)), int(m.group(2))
            if k not in idle_latest or r >= idle_latest[k][0]:
                idle_latest[k] = (r, [int(m.group(i)) for i in range(3, 9)])
        for _, v in idle_latest.values():
            idle_totals = [a + b for a, b in zip(idle_totals, v)]
    for key, v in sorted(totals.items(), key=lambda kv: str(kv[0])):
        n = sum(v) or 1
        print(f"{str(key):5} " + " ".join(f"{name} {100 * x / n:4.1f}%" for name, x in zip(KEYS, v)))
    n = sum(idle_totals) or 1
    print("idle causes " + " ".join(f"{name} {100 * x / n:4.1f}%" for name, x in zip(IDLE_KEYS, idle_totals)))
    print(f"stalled games (a champion idle > 20% of its turns): {stalled} of {games}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

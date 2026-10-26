"""Find period-2 position oscillations (A, B, A, B, ...) in per-turn position traces of bots/main.

Usage: python -B bots/main/tools/oscillation.py PATH ... [--side a|b] [--bot SEL]
PATH: local replays or run directories (see replays.py). Needs one of two traces:
  T lines  knobs only: debug_trace_hi=2000 debug_trace_champion=<slot 0-4> (one champion per game;
           the 400-line log cap per champion instance and the camp detail lines bound the window,
           so narrow it with debug_trace_lo/hi);
  Q lines  every champion: a trace copy of the bot (never a line's source) with, at the end of
           Brain::turn, MB_LOG("Q %d %d %d %d %s %s %d\\n", round, type, x, y, mode, why_, target_);
           and the log cap in main.cpp (log_allowed) raised to ~4000.
Instrumentation costs fuel (~1K per formatted line), so a traced game can diverge from the real one.
Reports the share of champion turns spent alternating between two tiles (runs of >= 6 turns), per
map and per spot, with the two modes involved. Found the wall-corner fight/travel swap (D70) and
farm/retreat standoffs.
"""
import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replays  # noqa: E402

Q = re.compile(r"^Q (\d+) (\d+) (\d+) (\d+) (\S+) (\S+) (-?\d+)$", re.M)
T = re.compile(r"^T r(\d+) p(\d+),(\d+) hp\S+ ([^/\s]+)/(\S*) e-?\d+ t(-?\d+)", re.M)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    replays.add_arguments(p)
    args = p.parse_args()
    spots = Counter()
    per_map = Counter()
    example = {}
    total = 0
    for game in replays.games(args.paths, args.side, args.bot):
        where = game.map or Path(game.source.split("!")[0]).stem
        seq = defaultdict(list)
        for m in Q.finditer(game.text):
            r, k, x, y, mode, why, _ = m.groups()
            seq[k].append((int(r), (int(x), int(y)), f"{mode}/{why}"))
        for m in T.finditer(game.text):
            r, x, y, mode, why, _ = m.groups()
            seq["T"].append((int(r), (int(x), int(y)), f"{mode}/{why}"))
        for s in seq.values():
            total += len(s)
            run = 0
            for i in range(2, len(s) + 1):
                ok = i < len(s) and s[i][0] - s[i - 2][0] == 2 and s[i][1] == s[i - 2][1] and s[i][1] != s[i - 1][1]
                if ok:
                    run += 1
                    continue
                if run >= 4:
                    a, b = s[i - 2], s[i - 1]
                    key = (where, tuple(sorted([a[1], b[1]])), f"{b[2]} <-> {a[2]}")
                    spots[key] += run + 2
                    per_map[where] += run + 2
                    example.setdefault(key, (Path(game.source.split("!")[0]).name, game.side, b[0]))
                run = 0
    osc = sum(per_map.values())
    print(f"oscillating turns {osc} of {total} traced ({100 * osc / max(1, total):.2f}%)")
    for mp, v in per_map.most_common(8):
        print(f"  {mp:22s} {v}")
    print("top spots:")
    for k, v in spots.most_common(12):
        print(f"  {v:5d} {k}  e.g. {example[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

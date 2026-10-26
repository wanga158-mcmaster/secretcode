"""Economy speedrun (bots/main): rounds our bot needs to bank the winning gems against a passive opponent.

  python -B bots/main/tools/speedrun.py RUN_DIR [--opponent starter-cpp] [--by-map]
  python -B bots/main/tools/speedrun.py --play [--var "NAME: k=v ..."] [--maps pool] [--out DIR]

An opponent-independent economy measure (README "Evaluation and release", D89): against the
starter, which rarely contests anything, the round count of a won game is our throughput from
draft to 1000 banked gems. RUN_DIR is any arena run, sweep or gauntlet directory (games.jsonl):
games against the passive opponent are grouped by variant; a loss or draw counts as max_rounds.
--play runs `arena sweep main/main --vs anchors/starter-cpp` first (all pool maps, both sides).
Prints per variant the mean rounds, the paired mean difference vs base on identical (map, side),
and with --by-map one row per map.
"""
from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
MAX_ROUNDS = 2000


def load(run: Path, opponent: str) -> dict:
    """variant -> {(map, side): rounds}."""
    out: dict = defaultdict(dict)
    for line in (run / "games.jsonl").read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        r = json.loads(line)
        tags = r.get("tags") or {}
        side = tags.get("subject")
        opp = tags.get("opponent") or (r["b"] if side == "A" else r["a"])
        if opponent not in str(opp) or side not in ("A", "B"):
            continue
        won = r.get("winner") == side
        out[tags.get("variant", "base")][(r["map"], side)] = r["rounds"] if won else MAX_ROUNDS
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("run", nargs="?")
    p.add_argument("--opponent", default="starter-cpp")
    p.add_argument("--by-map", action="store_true")
    p.add_argument("--play", action="store_true")
    p.add_argument("--var", action="append", default=[])
    p.add_argument("--maps", default="pool")
    p.add_argument("--out", default="")
    a = p.parse_args()
    run = Path(a.run) if a.run else None
    if a.play:
        out = a.out or str(ROOT / ".cache" / "main" / "speedrun")
        cmd = [str(ROOT / "arena.cmd"), "sweep", "main/main", "--vs", "anchors/starter-cpp", "--maps", a.maps, "--out", out]
        for v in a.var or ["same:"]:
            cmd += ["--var", v]
        subprocess.run(cmd, cwd=ROOT, check=False, capture_output=True)
        run = Path(out)
    if not run or not (run / "games.jsonl").exists():
        print("give a run directory with games.jsonl (or --play)")
        return 2
    data = load(run, a.opponent)
    if not data:
        print(f"no games against {a.opponent} in {run}")
        return 1
    base = data.get("base", {})
    for v, games in sorted(data.items(), key=lambda kv: kv[0] != "base"):
        keys = [k for k in games if k in base]
        line = f"{v:12s} games {len(games):3d} mean rounds {statistics.mean(games.values()):7.1f}" \
               f"  unfinished {sum(x >= MAX_ROUNDS for x in games.values())}"
        if v != "base" and keys:
            diffs = [games[k] - base[k] for k in keys]
            se = statistics.stdev(diffs) / len(diffs) ** 0.5 if len(diffs) > 1 else 0.0
            line += f"  paired diff {statistics.mean(diffs):+7.1f} +- {se:.1f} se (n={len(keys)})"
        print(line)
    if a.by_map:
        maps = sorted({m for g in data.values() for m, _ in g})
        names = sorted(data, key=lambda v: v != "base")
        print(f"{'map':24s} " + " ".join(f"{v[:10]:>10s}" for v in names))
        for m in maps:
            cells = []
            for v in names:
                vals = [data[v][k] for k in ((m, "A"), (m, "B")) if k in data[v]]
                cells.append(f"{statistics.mean(vals):10.0f}" if vals else f"{'-':>10s}")
            print(f"{m:24s} " + " ".join(cells))
    return 0


if __name__ == "__main__":
    sys.exit(main())

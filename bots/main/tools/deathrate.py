"""Our deaths by round R per variant and death class in a local run (bots/main).

  python -B bots/main/tools/deathrate.py RUN_DIR [--rounds 400] [--by opponent|map|none]

RUN_DIR is an arena sweep or gauntlet directory run with --keep-replays all: games.jsonl rows
(tags: variant, subject side, opponent) and their replays. Our side's death lines ("death r<round>
k<type> <notes>", every release build) are read from the replay's team stdout and classified like
tools/deaths.py (stuck, camp, burst, fought, standoff, chased). Prints, per variant, deaths per game
by round R in total and by class, and the paired difference vs base on identical games.
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
import zipfile
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deaths  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]


def game_deaths(replay: Path, side: str, rounds: int) -> Counter | None:
    try:
        with zipfile.ZipFile(replay) as z:
            text = z.read(f"team-{side.lower()}.stdout").replace(b"\x00", b"").decode("utf-8", "replace")
    except (OSError, KeyError, zipfile.BadZipFile):
        return None
    out = Counter()
    for line in text.splitlines():
        h = deaths.HEAD.match(line.strip())
        if not h or int(h.group(1)) > rounds:
            continue
        notes = [dict(dt=int(m[0]), hp=int(m[1]), mode=m[2], why=m[3], e=int(m[4]), d=int(m[5]), a=int(m[6]),
                      our=int(m[7]), their=int(m[8])) for m in deaths.NOTE.findall(line)]
        out[deaths.classify(notes)] += 1
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("run")
    p.add_argument("--rounds", type=int, default=400)
    p.add_argument("--by", choices=["opponent", "map", "none"], default="none")
    a = p.parse_args()
    run = Path(a.run)
    per = defaultdict(dict)  # variant -> key -> Counter
    for line in (run / "games.jsonl").read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        r = json.loads(line)
        tags = r.get("tags") or {}
        rep = r.get("replay")
        if not rep:
            continue
        path = Path(rep) if Path(rep).is_absolute() else ROOT / rep
        c = game_deaths(path, tags.get("subject", "A"), a.rounds)
        if c is None:
            continue
        per[tags.get("variant", "base")][(tags.get("opponent", "?"), r["map"], tags.get("subject"), tags.get("seed", 0))] = c
    if not per:
        print("no replays with our logs (run with --keep-replays all)")
        return 1
    classes = ["stuck", "camp", "burst", "fought", "standoff", "chased"]
    base = per.get("base", {})
    print(f"deaths by r{a.rounds} per game")
    print(f"{'variant':10s} {'group':24s} {'games':>5s} {'total':>6s} " + " ".join(f"{c:>8s}" for c in classes) + "  paired vs base")
    for v in sorted(per, key=lambda x: x != "base"):
        groups = defaultdict(list)
        for k, c in per[v].items():
            g = {"opponent": k[0], "map": k[1], "none": "all"}[a.by]
            groups[g].append((k, c))
        for g, items in sorted(groups.items()):
            n = len(items)
            tot = sum(sum(c.values()) for _, c in items)
            line = f"{v:10s} {g[:24]:24s} {n:5d} {tot / n:6.2f} " + " ".join(f"{sum(c[x] for _, c in items) / n:8.2f}" for x in classes)
            if v != "base":
                diffs = [sum(c.values()) - sum(base[k].values()) for k, c in items if k in base]
                if len(diffs) > 1:
                    line += f"  {statistics.mean(diffs):+.2f} +- {statistics.stdev(diffs) / len(diffs) ** 0.5:.2f} (n={len(diffs)})"
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())

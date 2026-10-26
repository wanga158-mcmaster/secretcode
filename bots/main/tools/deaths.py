"""Classify bots/main death reports ("death r<round> k<type> <dt>:<hp>:<mode>/<why>:e<n>d<dist>a<allies>:s<our>-<their> ...").

Usage: python -B bots/main/tools/deaths.py PATH ... [--side a|b] [--bot SEL]
PATH: local replays or run directories, online downloads under results/online/ (see replays.py).
Death lines are in every release build's log. Categories (first match wins), from the 10-turn
history before each death:
  stuck     >= 5 consecutive low-health notes, health falling each turn, no enemy in view
            (regression signature: recalling under an unseen camp's fire)
  camp      no enemy visible in the last 3 notes
  burst     died from >= 300 hp in one round
  fought    the history contains a favourable simulation (we lose fewer than them)
  standoff  >= 3 consecutive notes with unchanged hp while an enemy is within 4 tiles
  chased    anything else with enemies in view
"""
import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replays  # noqa: E402

NOTE = re.compile(r"(-?\d+):(\d+):(\w*)/([\w-]*):e(\d+)d(\d+)a(\d+):s(-?\d+)-(-?\d+)")
HEAD = re.compile(r"death r(\d+) k(\d+)")


def classify(notes):
    if not notes:
        return "empty"
    run = 1
    for a, b in zip(notes, notes[1:]):
        run = run + 1 if (a["mode"] == b["mode"] == "lo" and b["hp"] < a["hp"] and b["e"] == 0) else 1
        if run >= 5:
            return "stuck"
    if all(n["e"] == 0 for n in notes[-3:]):
        return "camp"
    if notes[-1]["hp"] >= 300:
        return "burst"
    if any(0 <= n["our"] < n["their"] for n in notes):
        return "fought"
    run = 1
    for a, b in zip(notes, notes[1:]):
        run = run + 1 if (b["hp"] == a["hp"] and b["e"] > 0 and b["d"] <= 4) else 1
        if run >= 3:
            return "standoff"
    return "chased"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    replays.add_arguments(p)
    args = p.parse_args()
    cats = Counter()
    by_type = defaultdict(Counter)
    contact_hp = defaultdict(list)
    n_games = 0
    for game in replays.games(args.paths, args.side, args.bot):
        n_games += 1
        for line in game.text.splitlines():
            h = HEAD.match(line)
            if not h:
                continue
            notes = [dict(dt=int(m[0]), hp=int(m[1]), mode=m[2], why=m[3], e=int(m[4]), d=int(m[5]), a=int(m[6]),
                          our=int(m[7]), their=int(m[8])) for m in NOTE.findall(line)]
            c = classify(notes)
            cats[c] += 1
            by_type[int(h.group(2))][c] += 1
            first = next((n for n in notes if n["e"] > 0), None)
            if first:
                contact_hp[c].append(first["hp"])
    total = sum(cats.values()) or 1
    print(f"{sum(cats.values())} deaths in {n_games} game sides")
    for c, k in cats.most_common():
        hps = sorted(contact_hp[c])
        med = hps[len(hps) // 2] if hps else -1
        print(f"{c:9} {k:5} {100 * k / total:5.1f}%  median hp at first sighting {med}")
    for t, cnt in sorted(by_type.items()):
        print(f"  k{t:<2} " + " ".join(f"{c}:{v}" for c, v in cnt.most_common()))
    return 0


if __name__ == "__main__":
    sys.exit(main())

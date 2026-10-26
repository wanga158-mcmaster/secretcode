"""Calibrate the bots/main PvP fight simulation against outcomes (needs P-line traces).

Usage: python -B bots/main/tools/pvpcal.py PATH ... [--side a|b] [--bot SEL]
PATH: local replays or run directories with the games' statistics JSON (see replays.py).
The bot must log P-lines ("P r<round> k<type> p<x>,<y> hp.. g.. <mode> s<our>-<their> act<mat> | ..."):
knobs debug_pvp_lo / debug_pvp_hi (one line per turn with enemies in view; the 400-line log cap per
champion instance bounds the window). Each champion's P-lines split into engagements (gaps > 2
rounds); an engagement is bucketed by its first simulation (our vs their predicted losses) and by
whether we attacked during it. Outcome window: the engagement plus 10 rounds. Reports, per bucket:
engagements, share where our champion died, enemy deaths within 6 tiles of our engagement
positions per engagement (kills from the statistics). This is the fight model's calibration
check: a "favourable" bucket in which we die often means the simulation overrates us.
"""
import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import replays  # noqa: E402

P = re.compile(r"^P r(\d+) k(\d+) p(\d+),(\d+) hp(\d+) g(\d+) (\w+) s(-?[\d.]+)-(-?[\d.]+) act(\d)(\d)(\d)")
NAMES = ["Ryth", "Vesper Vale", "Vendetta", "Cassi", "Issac", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg", "Tavi",
         "Heart of Ruin"]


def bucket(our: float, their: float) -> str:
    if our < 0:
        return "no-sim"
    if their >= our + 0.5:
        return "favourable"
    if our >= their + 0.5:
        return "unfavourable"
    return "even"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    replays.add_arguments(p)
    args = p.parse_args()
    stats = defaultdict(lambda: [0, 0, 0.0])  # bucket -> engagements, our deaths, enemy deaths
    no_stats = 0
    for game in replays.games(args.paths, args.side, args.bot):
        if not game.stats:
            no_stats += 1
            continue
        team = game.team
        kills = []  # (round, victim team, victim name, x, y)
        for e in game.stats.get("events", []):
            if e["kind"] != "kill":
                continue
            for en in e["entries"]:
                t = en.get("target") or {}
                if t.get("kind") == "champion":
                    kills.append((en["timestamp"]["round"], t.get("team"), t.get("name"), en.get("x"), en.get("y")))
        per_champ = defaultdict(list)
        for line in game.text.splitlines():
            m = P.match(line)
            if m:
                per_champ[int(m.group(2))].append(m)
        for k, lines in per_champ.items():
            name = NAMES[k] if k < len(NAMES) else str(k)
            eng = []
            for m in lines + [None]:
                if m is not None and eng and int(m.group(1)) - int(eng[-1].group(1)) <= 2:
                    eng.append(m)
                    continue
                if eng:
                    first = eng[0]
                    r0, r1 = int(first.group(1)), int(eng[-1].group(1)) + 10
                    attacked = any(x.group(11) == "1" for x in eng)  # basic attack (casts include escape dashes)
                    b = bucket(float(first.group(8)), float(first.group(9))) + (" fought" if attacked else " held/left")
                    died = any(r0 <= r <= r1 and vt == team and vn == name for r, vt, vn, _, _ in kills)
                    pts = [(int(x.group(3)), int(x.group(4))) for x in eng]
                    enemy = sum(1 for r, vt, _, x, y in kills if r0 <= r <= r1 and vt != team and x is not None and
                                any(max(abs(x - px), abs(y - py)) <= 6 for px, py in pts))
                    st = stats[b]
                    st[0] += 1
                    st[1] += died
                    st[2] += enemy
                eng = [m] if m is not None else []
    if no_stats:
        print(f"note: {no_stats} game sides without statistics skipped")
    print(f"{'bucket':28s} {'n':>6s} {'we died':>8s} {'enemy deaths/eng':>17s}")
    for b in sorted(stats):
        n, d, e = stats[b]
        print(f"{b:28s} {n:6d} {100 * d / max(1, n):7.1f}% {e / max(1, n):17.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

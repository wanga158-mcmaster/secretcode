"""Per-turn damage taken (HP at the end of the previous own turn minus HP at this turn start)
and hits dealt, from probe replays.

  python -I probe_damage.py RUN_DIR [--who NAME] [--rounds A:B]
"""
from __future__ import annotations

import argparse
import re
import sys
import zipfile
from collections import Counter
from pathlib import Path

ST = re.compile(r"^R(\d+) (\w+) hp([\d.]+)/([\d.]+) prev(-?[\d.]+) str([\d.]+) foc([\d.]+) p(\d+),(\d+)")
HIT = re.compile(r"^(ATK2?) r(\d+) (\S)@(\d+),(\d+) ([\d.]+)->([\d.]+) d([\d.]+)")


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("run")
    p.add_argument("--who", default="")
    p.add_argument("--rounds", default="")
    a = p.parse_args()
    lo, hi = (int(x) for x in a.rounds.split(":")) if a.rounds else (0, 1 << 30)
    for rep in sorted(Path(a.run).rglob("*.repl26a")):
        with zipfile.ZipFile(rep) as z:
            text = z.read("team-a.stdout").decode("utf-8", "replace").replace("\x00", "")
        dmg = Counter()
        hits = Counter()
        who = None
        for raw in text.splitlines():
            line = raw.strip()
            m = ST.match(line)
            if m:
                who = m.group(2)
                r = int(m.group(1))
                if a.who and who != a.who or not (lo <= r <= hi):
                    continue
                hp, prev = float(m.group(3)), float(m.group(5))
                if prev >= 0 and abs(prev - hp) > 1e-6:
                    dmg[(who, round(prev - hp, 4))] += 1
                continue
            m = HIT.match(line)
            if m and who and (not a.who or who == a.who) and lo <= int(m.group(2)) <= hi:
                hits[(who, m.group(3), round(float(m.group(8)), 4))] += 1
        print(f"== {rep.parent.parent.name}")
        print("  damage taken per turn (who, amount: count):", ", ".join(f"{w} {d:g}:{n}" for (w, d), n in sorted(dmg.items())))
        print("  hits dealt (who, target, damage: count):", ", ".join(f"{w} {t} {d:g}:{n}" for (w, t, d), n in sorted(hits.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())

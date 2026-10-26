"""Calibrate the clear simulator (econ.simulate_clear) against the economy probes.

  python -I calibrate.py [--probes .cache/contenders/economy/probes]

For each probe run: measured first hit, last kill (the turn before the group first reads
'reward'), engaged span, damage taken by the active champions, recalls and deaths, next to the
simulator's prediction for the same fighters and group (positions from the map geometry).
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import econ  # noqa: E402

ST = re.compile(r"^R(\d+) (\w+) hp([\d.]+)/([\d.]+) prev(-?[\d.]+)")
FIRST = re.compile(r"^(ATK2?|SPELL|AOE|DASH) r(\d+)")
REWARD_STATE = re.compile(r"^R(\d+) .*\| reward")
RECALL = re.compile(r"^RECALL r(\d+)")
DEAD = re.compile(r"^R(\d+) (\w+) dead")

# probe -> (map, group member tile, fighters in tank order (cid), shards, emblems, spells, measured-phase start round)
CASES = {
    "p1": ("00packs", (8, 2), ["Vendetta"], {}, False, False),
    "p2": ("00packs", (8, 2), ["Vendetta"], {}, True, True),
    "p3": ("00packs", (8, 2), ["Vesper"], {}, False, True),
    "p4": ("00packs", (8, 2), ["Vendetta", "Vesper"], {}, False, True),
    "y4": ("01yellow_brick_road", (9, 14), ["Vendetta"], {"Vendetta": ((0, 5, 0), ("monster",))}, False, False),
    "e1": ("00elkring", (9, 2), ["Vendetta"], {}, True, True),
    "e2": ("00elkring", (9, 2), ["Vendetta", "Marcella"], {}, True, True),
    "e3": ("00elkring", (9, 2), ["Vendetta", "Vesper"], {}, True, True),
    "e4": ("00elkring", (9, 2), ["Ryth"], {}, True, True),
    "e5": ("00elkring", (9, 2), ["Varg"], {}, True, True),
    "e6": ("00elkring", (9, 2), ["Ryth", "Vendetta", "Marcella"], {}, True, True),
    "e7": ("00elkring", (9, 2), ["Ryth", "Vesper", "Vendetta", "Marcella", "Heart"], {}, True, True),
    "e8": ("00elkring", (9, 2), ["Vendetta"], {}, False, True),
    "g1": ("00quadrants", (31, 13), ["Ryth", "Vesper", "Vendetta", "Marcella", "Heart"], {}, True, True),
    "m1": ("01broken_curve", (11, 17), ["Ryth", "Vendetta", "Marcella"], {}, True, True),
    "m2": ("00lost_world", (1, 15), ["Ryth", "Vesper", "Vendetta", "Marcella", "Heart"], {}, True, True),
}


def measured(run: Path):
    reps = sorted(run.rglob("*.repl26a"))
    if not reps:
        return None
    with zipfile.ZipFile(reps[0]) as z:
        text = z.read("team-a.stdout").decode("utf-8", "replace").replace("\x00", "")
    first = last = None
    taken = 0.0
    recalls = deaths = 0
    seen_dead = set()
    phase2 = False
    for raw in text.splitlines():
        line = raw.strip()
        if " ph2 " in line:
            phase2 = True
        if not phase2:
            continue
        m = FIRST.match(line)
        if m and first is None:
            first = int(m.group(2))
        m = REWARD_STATE.match(line)
        if m and last is None and first is not None:
            last = int(m.group(1)) - 1
        m = ST.match(line)
        if m and last is None:
            hp, prev = float(m.group(3)), float(m.group(5))
            if prev >= 0 and prev > hp:
                taken += prev - hp
        if RECALL.match(line) and last is None:
            recalls += 1
        m = DEAD.match(line)
        if m and last is None and m.group(2) not in seen_dead:
            seen_dead.add(m.group(2))
            deaths += 1
    return {"first": first, "last": last, "span": (last - first + 1) if first and last else None,
            "taken": round(taken, 1), "recalls": recalls, "deaths": deaths}


def group_members(map_id, tile):
    geo = json.loads((econ.ROOT / "maps" / "geometry.json").read_text(encoding="utf-8"))
    m = next(x for x in geo["maps"] if x["id"] == map_id)
    g = next(g for g in m["groups"] if any((x["x"], x["y"]) == tile for x in g["members"]))
    return [(x["type"], x["x"], x["y"]) for x in g["members"]]


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--probes", default=str(econ.ROOT / ".cache" / "contenders" / "economy" / "probes"))
    a = p.parse_args()
    cat = econ.load_catalog()
    print(f"{'probe':5s} {'measured span/taken/recalls/deaths':38s} {'simulated span/taken/trips/deaths':36s}")
    for name, (map_id, tile, team, items, retreat, spells) in CASES.items():
        run = Path(a.probes) / name
        if not run.exists():
            continue
        meas = measured(run)
        mons = econ.monsters_of(cat, group_members(map_id, tile))
        fighters = []
        for i, n in enumerate(team):
            sh, em = items.get(n, ((0, 0, 0), ()))
            fighters.append(econ.make_fighter(cat, n, i, shards=sh, emblems=em))
        res = econ.simulate_clear(cat, fighters, mons, walk_back=5, use_spells=spells, retreat=retreat, reserve=1.0)
        ms = f"{meas['span']}/{meas['taken']}/{meas['recalls']}/{meas['deaths']}" if meas else "-"
        ss = f"{res.rounds if res.killed else 'x'}/{round(res.damage, 1)}/{res.trips}/{res.deaths}"
        print(f"{name:5s} {ms:38s} {ss:36s} {map_id} {'+'.join(team)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

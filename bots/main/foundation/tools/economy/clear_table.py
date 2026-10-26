"""Clear arithmetic tables (economy.md E7-E10): every kit, solo and in canonical teams, against
each monster type and the common pool compositions, at base stats and with +1 shard of each stat.

  python -I clear_table.py [--out bots/main/foundation/data/economy/clear_table.json] [--md]

Per (team, composition): rounds from the first hit to the last kill, damage absorbed, trips (1 = no
retreat), champion-rounds of the clear (fighting rounds x team size + rounds out recalling), and
gems / gold per champion-round of a full cycle: clear + capture (capture_time + 1, minus the kill
turn when a melee fighter lands the kill) + a travel allowance (default 10 rounds per capture per
champion). The walk back after a retreat is the same allowance. Compositions use their real
member layout from the pool map where they first occur.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import econ  # noqa: E402

OUT = econ.ROOT / "bots" / "main" / "foundation" / "data" / "economy" / "clear_table.json"
SINGLE = ["Ryth", "Vesper", "Vendetta", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg", "Tavi", "Heart",
          "Cassi", "Issac"]
TEAMS = {
    "Cassi+Issac": ["Issac", "Cassi"],
    "Vendetta+Vesper": ["Vendetta", "Vesper"],
    "Marcella+Ryth": ["Marcella", "Ryth"],
    "Heart+Stellar": ["Heart", "Stellar"],
    "Varg+Tavi": ["Varg", "Tavi"],
    "Ryth+Vendetta+Marcella": ["Ryth", "Marcella", "Vendetta"],
    "Heart+Stellar+Tavi": ["Heart", "Stellar", "Tavi"],
    "main five": ["Ryth", "Marcella", "Vendetta", "Heart", "Vesper"],
    "meta five": ["Ryth", "Marcella", "Heart", "Stellar", "Vesper"],
}


def compositions(top=24):
    geo = json.loads((econ.ROOT / "maps" / "geometry.json").read_text(encoding="utf-8"))
    count, layout = Counter(), {}
    for m in geo["maps"]:
        for g in m["groups"]:
            key = " ".join(f"{t.replace('_cluster', '')}{n}" for t, n in sorted(Counter(x["type"] for x in g["members"]).items()))
            count[key] += 1
            if key not in layout:
                x0 = min(x["x"] for x in g["members"])
                y0 = min(x["y"] for x in g["members"])
                layout[key] = [(x["type"], x["x"] - x0, x["y"] - y0) for x in g["members"]]
    singles = {"wolf1": [("wolf", 0, 0)], "elk1": [("elk", 0, 0)], "golem1": [("golem", 0, 0)], "monolith1": [("monolith", 0, 0)],
               "gem1": [("gem_cluster", 0, 0)]}
    comps = dict(singles)
    for key, n in count.most_common(top):
        comps.setdefault(key, layout[key])
    for key in ("elk3 gold3 wolf1", "elk4 gem2 gold2", "gem12 golem1", "gem4 monolith1", "gem2 monolith2"):
        if key in layout:
            comps.setdefault(key, layout[key])
    return comps, count


def rewards(members):
    gems = sum(econ.REWARD[t][0] for t, _, _ in members)
    gold = sum(econ.REWARD[t][1] for t, _, _ in members)
    cd = max(econ.RESPAWN[t] for t, _, _ in members)
    return gems, gold, cd


def run(cat, names, members, shards=None, travel=10, emblems=()):
    shards = shards or {}
    fighters = [econ.make_fighter(cat, n, i, shards=shards.get(n, (0, 0, 0)), emblems=emblems) for i, n in enumerate(names)]
    mons = econ.monsters_of(cat, members)
    r = econ.simulate_clear(cat, fighters, mons, walk_back=travel)
    gems, gold, cd = rewards(members)
    cap = int(cat.settings["capture_time"]) + (0 if r.killer_melee else 1)
    n = len(names)
    if not r.killed:
        return {"killed": False}
    fight_cr = r.rounds * n
    cycle_cr = fight_cr - r.out_rounds * 0 + cap + travel * n   # one capturer waits; everyone travels
    # champions out recalling are not fighting: count their absence as labour too
    cycle_cr = r.rounds * n + cap + travel * n
    gm = sum(f.gold_mult for f in fighters) / n
    return {"killed": True, "rounds": r.rounds, "damage": round(r.damage), "trips": r.trips, "deaths": r.deaths,
            "champion_rounds": cycle_cr, "gems_per_cr": round(gems / cycle_cr, 3), "gold_per_cr": round(gold * gm / cycle_cr, 2),
            "no_recall": r.trips == 1 and r.deaths == 0}


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", default=str(OUT))
    p.add_argument("--travel", type=int, default=10)
    p.add_argument("--md", action="store_true")
    a = p.parse_args()
    cat = econ.load_catalog()
    comps, count = compositions()
    table = {"settings": {k: cat.settings[k] for k in ("capture_time", "recall_time", "respawn_time")}, "travel": a.travel,
             "compositions": {}, "rows": []}
    for key, members in comps.items():
        gems, gold, cd = rewards(members)
        hp = sum(cat.camps[t]["hp"] for t, _, _ in members)
        st = sum(cat.camps[t]["str"] for t, _, _ in members)
        table["compositions"][key] = {"members": members, "gems": gems, "gold": gold, "cooldown": cd, "hp": hp,
                                      "strength": st, "pool_count": count.get(key, 0)}
    teams = {n: [n] for n in SINGLE}
    teams.update(TEAMS)
    for tname, names in teams.items():
        for key, members in comps.items():
            row = {"team": tname, "size": len(names), "comp": key, "base": run(cat, names, members, travel=a.travel)}
            for stat, idx in (("+str", 1), ("+focus", 2), ("+hp", 0)):
                sh = {n: tuple(1 if i == idx else 0 for i in range(3)) for n in names}
                row[stat] = run(cat, names, members, shards=sh, travel=a.travel)
            table["rows"].append(row)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps(table, indent=1) + "\n", encoding="utf-8")
    if a.md:
        keys = ["wolf1", "elk1", "golem1", "gold1 wolf1", "gem1 wolf1", "gold2 wolf2", "elk1 gem1 gold1", "elk2 gold2"]
        print("| team | " + " | ".join(keys) + " |")
        print("|---|" + "---|" * len(keys))
        for tname in teams:
            cells = []
            for k in keys:
                row = next((r for r in table["rows"] if r["team"] == tname and r["comp"] == k), None)
                b = row["base"] if row else {"killed": False}
                cells.append(f"{b['rounds']}r {b['damage']}d{'' if b['no_recall'] else ' x' + str(b['trips'])}" if b.get("killed") else "-")
            print(f"| {tname} | " + " | ".join(cells) + " |")
    return 0


if __name__ == "__main__":
    sys.exit(main())

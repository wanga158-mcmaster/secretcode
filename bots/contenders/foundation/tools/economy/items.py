"""Item economics (economy.md E17-E20): the marginal throughput value of each shard and emblem.

  python -I items.py [--out bots/contenders/foundation/data/economy/items.json]

For every kit and every common composition (data/economy/clear_table.json layouts), the clear
simulator is run at base and with one more shard of Strength, Focus or Health, and with each camp
emblem (Jungle, Durability, Monster) on top of its five shards. Reported per item:
- d_rounds: change in clear rounds (first hit to last kill);
- d_damage: change in damage absorbed;
- d_cycle: change in champion-rounds per capture, counting the HP cost as recall trips
  (damage / usable health x (recall_time + 1 + walk back)); this is the throughput effect;
- break-even captures: how many captures of that composition repay a dedicated shop trip
  (recall_time + 1 + walk back) with that item's d_cycle.
Gold buys nothing else in the race (lifetime earned gold, the round-2000 tiebreak, is not reduced
by spending), so the price of an item is the trip that fetches it, never its gold.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import econ  # noqa: E402

DATA = econ.ROOT / "bots" / "contenders" / "foundation" / "data" / "economy"
KITS = ["Ryth", "Vesper", "Vendetta", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg", "Tavi", "Heart", "Cassi", "Issac"]
COMPS = ["wolf1", "gold1 wolf1", "gem1 wolf1", "gold2 wolf2", "elk1 gem1 gold1", "elk1", "elk2 gold2", "golem1", "gem1", "gold2"]


def cycle_cost(cat, res, max_hp, walk_back, capture):
    """Champion-rounds of one capture by one champion: fight + capture + health cost in trips."""
    usable = max_hp * 0.8
    trips_hp = res.damage / usable
    return res.rounds + capture + trips_hp * (int(cat.settings["recall_time"]) + 1 + walk_back)


def evaluate(cat, kit, members, shards=(0, 0, 0), emblems=(), walk_back=15):
    f = econ.make_fighter(cat, kit, 0, shards=shards, emblems=emblems)
    # no retreats inside the fight: the health cost is charged as trips by cycle_cost
    f.max_hp = f.hp = 1e9
    r = econ.simulate_clear(cat, [f], econ.monsters_of(cat, members), walk_back=walk_back, retreat=False)
    hp = econ.make_fighter(cat, kit, 0, shards=shards, emblems=emblems).max_hp
    cap = int(cat.settings["capture_time"]) + (0 if r.killer_melee else 1)
    return r, cycle_cost(cat, r, hp, walk_back, cap)


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--out", default=str(DATA / "items.json"))
    p.add_argument("--walk-back", type=int, default=15)
    a = p.parse_args()
    cat = econ.load_catalog()
    table = json.loads((DATA / "clear_table.json").read_text(encoding="utf-8"))
    comps = {k: [tuple(x) for x in v["members"]] for k, v in table["compositions"].items()}
    trip = int(cat.settings["recall_time"]) + 1 + a.walk_back
    rows = []
    for kit in KITS:
        for comp in COMPS:
            if comp not in comps:
                continue
            base_r, base_c = evaluate(cat, kit, comps[comp], walk_back=a.walk_back)
            row = {"kit": kit, "comp": comp, "base_rounds": base_r.rounds, "base_damage": round(base_r.damage),
                   "base_cycle": round(base_c, 1), "items": {}}
            variants = {"str shard": ((0, 1, 0), ()), "focus shard": ((0, 0, 1), ()), "health shard": ((1, 0, 0), ()),
                        "5 str + Monster": ((0, 5, 0), ("monster",)), "5 str (Power=6)": ((0, 6, 0), ()),
                        "5 health + Jungle": ((5, 0, 0), ("jungle",)), "5 health + Durability": ((5, 0, 0), ("durability",)),
                        "5 str + Jungle": ((0, 5, 0), ("jungle",)), "5 focus + Jungle": ((0, 0, 5), ("jungle",)),
                        "5 focus (Power=6)": ((0, 0, 6), ())}
            for name, (sh, em) in variants.items():
                r, c = evaluate(cat, kit, comps[comp], shards=sh, emblems=em, walk_back=a.walk_back)
                d_cycle = c - base_c
                row["items"][name] = {"d_rounds": r.rounds - base_r.rounds, "d_damage": round(r.damage - base_r.damage),
                                      "d_cycle": round(d_cycle, 2),
                                      "breakeven_captures": round(trip / -d_cycle, 1) if d_cycle < -1e-9 else None}
            rows.append(row)
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps({"walk_back": a.walk_back, "shop_trip": trip, "rows": rows}, indent=1) + "\n", encoding="utf-8")
    # compact print: per kit, d_cycle per 150 gold for the emblem options on wolf / elk content
    for kit in KITS:
        line = [kit]
        for comp in ("gold1 wolf1", "elk1 gem1 gold1", "gold2 wolf2"):
            row = next(r for r in rows if r["kit"] == kit and r["comp"] == comp)
            best = sorted(row["items"].items(), key=lambda kv: kv[1]["d_cycle"])[:3]
            line.append(f"{comp}: base {row['base_cycle']} best " + ", ".join(f"{n} {v['d_cycle']:+.1f}" for n, v in best))
        print(" | ".join(line))
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Purchase-order experiment (economy.md E20): the same schedules (each map's best policy from
data/economy/capacity.json) with different emblem plans (speedrun.ORDERS), items bought at spawn.

  python -I items_order.py [--maps pool] [--out bots/contenders/foundation/data/economy/items_order.json]
"""
from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import econ  # noqa: E402
import speedrun as sr  # noqa: E402


def job(args):
    key, order = args
    maps = sr.load_maps()
    cat = econ.load_catalog()
    sr.CAT[0] = cat
    rec, trav, geom = maps[key]
    cap = json.loads((sr.DATA / "economy" / "capacity.json").read_text(encoding="utf-8"))[key]
    pol = cap["policy"]
    mm = sr.MapModel(cat, rec, trav, geom, "full")
    r = sr.run_schedule(mm, sr.ROSTERS["main"][: rec["draft"]], B=pol["bank_at"], wait_max=pol["wait_max"], split=pol["split"],
                        items="spawn", lam=pol["gold_weight"], order=order)
    return key, order, r["rounds"], r["bank_at"].get(400, r["bank_at"].get("400"))


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--maps", default="pool")
    p.add_argument("--out", default=str(sr.DATA / "economy" / "items_order.json"))
    p.add_argument("--jobs", type=int, default=12)
    a = p.parse_args()
    keys = [k for k in sr.load_maps() if not k.startswith("legacy/")] if a.maps == "pool" else a.maps.split(",")
    jobs = [(k, o) for k in keys for o in sr.ORDERS]
    import multiprocessing as mp
    with mp.Pool(a.jobs) as pool:
        res = pool.map(job, jobs)
    out = {}
    for k, o, rounds, b400 in res:
        out.setdefault(k, {})[o] = {"rounds": rounds, "bank400": b400}
    Path(a.out).write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    for o in sr.ORDERS:
        vals = [out[k][o]["rounds"] for k in out]
        base = [out[k]["jungle"]["rounds"] for k in out]
        d = [x - y for x, y in zip(vals, base)]
        print(f"{o:16s} mean {statistics.mean(vals):6.1f}  vs jungle {statistics.mean(d):+6.1f} +- "
              f"{statistics.stdev(d) / len(d) ** 0.5 if len(d) > 1 else 0:.1f} (n {len(d)}), better on {sum(1 for x in d if x < 0)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

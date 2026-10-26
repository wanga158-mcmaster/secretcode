"""Death hazard per champion-round against carried bag, contact and health (combat.md C21; economy E22).

  python -I -X utf8 hazard.py [--workers 6]

Read-only over the online evidence: replay tracks `.cache/contenders/online/tracks/<mid>.json.gz`
(per actor and round at its turn start: x, y, health, gold, gems; written by tools/online/tracks.py),
match summaries (death events with context "champion"), and sides.jsonl (tier per side). A sample is
one alive champion at one turn start; the event is its death to a champion in that round. Writes
bots/contenders/foundation/data/combat/hazard.json.
"""
from __future__ import annotations

import collections
import concurrent.futures as cf
import gzip
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[4]
sys.path.insert(0, str(ROOT / "bots/contenders/foundation/tools/online"))
from common import jload, summary_paths  # noqa: E402

OUT = ROOT / ".cache/contenders/online"
TR = OUT / "tracks"
DATA = ROOT / "bots/contenders/foundation/data/combat/hazard.json"
BAGS = [(0, 0), (1, 9), (10, 19), (20, 39), (40, 79), (80, 10 ** 6)]


def bag_bucket(g: int) -> str:
    for lo, hi in BAGS:
        if lo <= g <= hi:
            return f"{lo}-{hi}" if hi < 10 ** 6 else f"{lo}+"
    return "?"


def contact_class(dmin: int, n3: int) -> str:
    if dmin > 6:
        return "none within 6"
    if n3 == 0:
        return "4-6 only"
    return "1 within 3" if n3 == 1 else "2+ within 3"


def hp_bucket(f: float) -> str:
    return "<40%" if f < 0.4 else ("40-70%" if f < 0.7 else ">=70%")


def analyse(args):
    mid, sp, tiers = args
    p = TR / f"{mid}.json.gz"
    try:
        with gzip.open(p, "rt", encoding="utf-8") as f:
            tr = json.load(f)
        s = jload(sp)
    except (OSError, ValueError):
        return {}
    team_of, samples = {}, collections.defaultdict(dict)
    hpmax = collections.defaultdict(float)
    for t in tr["tracks"]:
        team_of[t["id"]] = t["team"]
        for r, x, y, hp, gold, gems in t["s"]:
            if x >= 0 and hp > 0:
                samples[r][t["id"]] = (x, y, hp, gems)
                hpmax[t["id"]] = max(hpmax[t["id"]], hp)
    died = set()
    for e in s.get("events", []):
        if e.get("kind") != "death":
            continue
        for en in e["entries"]:
            if en.get("context") == "champion":
                died.add((e["owner"].get("id"), en["timestamp"]["round"]))
    out = collections.Counter()
    for r, P in samples.items():
        for a, (x, y, hp, gems) in P.items():
            ta = team_of[a]
            tier = tiers.get(ta, "T?")
            dmin, n3, al6 = 99, 0, 0
            for b, (bx, by, _, _) in P.items():
                if b == a:
                    continue
                d = max(abs(bx - x), abs(by - y))
                if team_of[b] != ta:
                    dmin = min(dmin, d)
                    n3 += d <= 3
                elif d <= 6:
                    al6 += 1
            ev = (a, r) in died
            keys = [("bag", bag_bucket(gems)), ("contact", contact_class(dmin, n3)),
                    ("bag|contact", bag_bucket(gems) + " | " + contact_class(dmin, n3)),
                    ("hp|contact", hp_bucket(hp / hpmax[a]) + " | " + contact_class(dmin, n3)),
                    ("tier|contact", tier + " | " + contact_class(dmin, n3)),
                    ("allies6|contact", f"{min(al6, 2)}{'+' if al6 >= 2 else ''} allies | " + contact_class(dmin, n3))]
            for k in keys:
                out[(k, "n")] += 1
                out[(k, "d")] += ev
    return out


def main(argv: list[str]) -> int:
    workers = int(argv[argv.index("--workers") + 1]) if "--workers" in argv else 6
    sp = summary_paths()
    tiers = collections.defaultdict(dict)
    for line in open(OUT / "sides.jsonl", encoding="utf-8"):
        row = json.loads(line)
        tiers[row["mid"]][0 if row["team"] == "teamA" else 1] = ("us" if row.get("name") == "ha²t" else row.get("tier", "T?"))
    mids = [q.name[:-8] for q in TR.glob("*.json.gz") if q.name[:-8] in sp]
    total = collections.Counter()
    with cf.ProcessPoolExecutor(max_workers=workers) as ex:
        for c in ex.map(analyse, [(m, sp[m], dict(tiers.get(m, {}))) for m in mids], chunksize=8):
            total.update(c)
    table = collections.defaultdict(dict)
    for (k, what), v in total.items():
        table[k[0]].setdefault(k[1], {"rounds": 0, "deaths": 0})["rounds" if what == "n" else "deaths"] += v
    for dim, rows in table.items():
        for key, row in rows.items():
            row["per_1000_rounds"] = round(1000.0 * row["deaths"] / row["rounds"], 3) if row["rounds"] else None
    res = {"games": len(mids), "event": "death to a champion in the round of the sample", "tables": table}
    DATA.write_text(json.dumps(res, indent=1, sort_keys=True), encoding="utf-8")
    for dim in ("bag", "contact", "bag|contact", "hp|contact", "allies6|contact", "tier|contact"):
        print("==", dim)
        for key, row in sorted(table[dim].items()):
            print(f"  {key:<36} rounds {row['rounds']:>9}  deaths {row['deaths']:>6}  per 1000 {row['per_1000_rounds']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

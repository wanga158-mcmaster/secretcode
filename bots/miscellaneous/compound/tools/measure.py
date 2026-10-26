"""Doctrine measures of contenders/compound from arena runs (statistics JSON kept next to replays).

  python -B bots/contenders/compound/tools/measure.py RUN_DIR [RUN_DIR ...] [--subject compound]
         [--by opponent|map|none] [--rounds 200,300,400]

For the subject's side of every game whose statistics JSON is present (run with
--keep-replays all): earned gold, emblems and team max HP at the given rounds, first purchase round
and shop sessions per champion (a session is purchases with gaps <= 3 rounds), recalls are not in
the statistics, deaths and kills by r400 and in the game, kills with at least one assist ("converged"
kills), gems banked at r400 and the game length of wins. Max HP is rebuilt from the purchases and
the engine catalog (base health + growth x health shards, Bruiser/Power extras), exactly as the
engine adds them (foundation M39).
"""
from __future__ import annotations

import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
CATALOG = ROOT / "docs" / "reference" / "runtime-catalog.json"

NAME_TO_CATALOG = {"Vesper Vale": "vesper_vale", "Heart of Ruin": "heart_of_ruin"}


def load_catalog() -> dict:
    cat = json.loads(CATALOG.read_text(encoding="utf-8"))["result"]
    return {c["name"]: c["stats"] for c in cat["champions"]}


def timeline_value(runs: list, rnd: int) -> float:
    t = 0
    last = 0.0
    for run in runs:
        if t + run["duration"] > rnd:
            return float(run["value"])
        t += run["duration"]
        last = float(run["value"])
    return last


def stat_of(item: str) -> str | None:
    low = item.lower()
    for s in ("health", "strength", "focus"):
        if low.endswith("of " + s):
            return s
    return None


def side_measures(st: dict, team: str, rounds: list[int], cat: dict) -> dict:
    champs = [c for c in st["roster"]["champions"] if c["team"] == team]
    ids = {c["id"]: c["name"] for c in champs}
    out: dict = {"n": len(champs)}
    # earned gold per round
    gold = defaultdict(float)
    for tl in st["championTimelines"]:
        if tl["metric"] == "earnedGold" and tl["owner"]["id"] in ids:
            for r in rounds:
                gold[r] += timeline_value(tl["runs"], r)
    for r in rounds:
        out[f"gold{r}"] = gold[r]
    # purchases and emblems
    buys = defaultdict(list)       # champion id -> [(round, item, count)]
    combos = defaultdict(list)     # champion id -> [(round, item)]
    deaths, kills = [], []
    for g in st["events"]:
        owner = g.get("owner") or {}
        oid = owner.get("id") if isinstance(owner, dict) else None
        if g["kind"] == "buyItem" and oid in ids:
            for e in g["entries"]:
                buys[oid].append((e["timestamp"]["round"], e["target"]["name"], int(e.get("value") or 1)))
        elif g["kind"] == "itemCombination" and oid in ids:
            for e in g["entries"]:
                combos[oid].append((e["timestamp"]["round"], e["target"]["name"]))
        elif g["kind"] == "death" and oid in ids:
            for e in g["entries"]:
                deaths.append(e["timestamp"]["round"])
        elif g["kind"] == "kill" and oid in ids:
            for e in g["entries"]:
                kills.append(e["timestamp"]["round"])
    assists = []
    for g in st["events"]:
        owner = g.get("owner") or {}
        if g["kind"] == "assist" and isinstance(owner, dict) and owner.get("id") in ids:
            for e in g["entries"]:
                assists.append(e["timestamp"]["round"])
    emblems = {r: 0 for r in rounds}
    hp = {r: 0.0 for r in rounds}
    first_shop, sessions = [], []
    for cid, name in ids.items():
        key = NAME_TO_CATALOG.get(name, name.lower().replace(" ", "_"))
        base = cat.get(key, {"base_health": 1000, "health_growth": 100})
        bl = sorted(buys[cid])
        if bl:
            first_shop.append(bl[0][0])
            n_sess, last = 0, -100
            for rr, _, _ in bl:
                if rr - last > 3:
                    n_sess += 1
                last = rr
            sessions.append(n_sess)
        else:
            sessions.append(0)
        for r in rounds:
            hshards = 0
            for rr, item, cnt in bl:
                if rr <= r and "Shard" in item and stat_of(item) == "health":
                    hshards += cnt
            for rr, item in combos[cid]:
                if rr > r:
                    continue
                emblems[r] += 1
                s = stat_of(item)
                if item.startswith("Power") and s == "health":
                    hshards += 1
                if item.startswith("Bruiser") and s != "health":
                    hshards += 1
            hp[r] += base["base_health"] + base["health_growth"] * hshards
    for r in rounds:
        out[f"emb{r}"] = emblems[r]
        out[f"hp{r}"] = hp[r]
    out["first_shop"] = statistics.median(first_shop) if first_shop else None
    out["sessions400"] = None
    s400 = []
    for cid in ids:
        bl = sorted(x for x in buys[cid] if x[0] <= 400)
        n_sess, last = 0, -100
        for rr, _, _ in bl:
            if rr - last > 3:
                n_sess += 1
            last = rr
        s400.append(n_sess)
    out["sessions400"] = statistics.mean(s400) if s400 else 0
    out["deaths400"] = sum(1 for d in deaths if d <= 400)
    out["deaths"] = len(deaths)
    out["kills400"] = sum(1 for k in kills if k <= 400)
    out["kills"] = len(kills)
    # a kill with an assist in the same round = at least two of ours hit the victim
    out["kills_assisted"] = sum(1 for k in kills if k in assists)
    for tl in st["teamTimelines"]:
        if tl["metric"] == "submittedGems" and tl["owner"] == team:
            out["banked400"] = timeline_value(tl["runs"], 400)
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("runs", nargs="+")
    p.add_argument("--subject", default="compound")
    p.add_argument("--by", default="opponent", choices=["opponent", "map", "none"])
    p.add_argument("--rounds", default="200,300,400")
    a = p.parse_args()
    rounds = [int(x) for x in a.rounds.split(",")]
    cat = load_catalog()
    groups: dict = defaultdict(list)
    for run in a.runs:
        rd = Path(run)
        for line in (rd / "games.jsonl").read_text(encoding="utf-8").splitlines():
            if not line.strip():
                continue
            row = json.loads(line)
            side = row.get("tags", {}).get("subject")
            if side not in ("A", "B"):
                side = "A" if a.subject in row.get("a_name", "") else "B"
            rep = row.get("replay")
            if not rep:
                continue
            stp = ROOT / Path(rep).with_suffix(".json")
            if not stp.exists():
                continue
            st = json.loads(stp.read_text(encoding="utf-8"))
            team = "teamA" if side == "A" else "teamB"
            m = side_measures(st, team, rounds, cat)
            m["win"] = 1 if row.get("winner") == side else 0
            m["rounds"] = row.get("rounds")
            opp = row["b_name"] if side == "A" else row["a_name"]
            key = opp if a.by == "opponent" else row["map"] if a.by == "map" else "all"
            groups[key].append(m)
    cols = [f"gold{r}" for r in rounds] + [f"emb{r}" for r in rounds] + [f"hp{r}" for r in rounds] + [
        "first_shop", "sessions400", "deaths400", "deaths", "kills400", "kills", "kills_assisted", "banked400"]
    print(f"{'group':28s} {'n':>3s} {'win':>4s} " + " ".join(f"{c[:9]:>9s}" for c in cols) + f" {'winrnd':>7s}")
    for key in sorted(groups):
        ms = groups[key]
        line = f"{key[:28]:28s} {len(ms):3d} {sum(x['win'] for x in ms) / len(ms):4.2f} "
        vals = []
        for c in cols:
            xs = [x[c] for x in ms if x.get(c) is not None]
            vals.append(f"{statistics.mean(xs):9.1f}" if xs else f"{'-':>9s}")
        wins = [x["rounds"] for x in ms if x["win"]]
        line += " ".join(vals) + f" {statistics.median(wins) if wins else 0:7.0f}"
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

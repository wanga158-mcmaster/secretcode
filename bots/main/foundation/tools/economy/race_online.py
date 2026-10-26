"""Race statistics from online match summaries (economy.md E24-E27): how games end, how banked
and gold leads at a given round predict the winner, what kills are worth in the race.

  python -I race_online.py [--root results/online] [--out bots/main/foundation/data/economy/race_online.json]

Reads every *-summary.json(.gz) under the root once per match id (read-only). Per game and team:
banked gems (teamTimelines submittedGems) and lifetime earned gold (championTimelines earnedGold)
at rounds 100..1000, kills, final gems, the end round and the winner.
"""
from __future__ import annotations

import argparse
import gzip
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
CHECK = (100, 200, 300, 400, 500, 600, 800, 1000)


def at(runs, idx):
    i = 0
    last = 0.0
    for r in runs:
        last = float(r["value"])
        if idx < i + r["duration"]:
            return last
        i += r["duration"]
    return last


def load(path: Path):
    raw = gzip.open(path).read() if path.suffix == ".gz" else path.read_bytes()
    return json.loads(raw)


def game_row(d, maps):
    m = d["match"]
    rounds = m["roundsCompleted"]
    out = {"map": maps.get(m["mapHash"], m["mapHash"][:8]), "rounds": rounds, "winner": m.get("outcome")}
    for t in d["teams"]:
        out[f"final_{t['team']}"] = int(t["submittedGems"])
    for tl in d["teamTimelines"]:
        if tl["metric"] == "submittedGems":
            out[f"bank_{tl['owner']}"] = {r: at(tl["runs"], r) for r in CHECK}
    gold = defaultdict(lambda: defaultdict(float))
    for tl in d["championTimelines"]:
        if tl["metric"] == "earnedGold":
            team = tl["owner"]["team"]
            for r in CHECK:
                gold[team][r] += at(tl["runs"], r)
    for team in ("teamA", "teamB"):
        out[f"gold_{team}"] = dict(gold[team])
    kills = defaultdict(list)
    for e in d["events"]:
        if e["kind"] == "kill":
            for x in e["entries"]:
                kills[e["owner"]["team"]].append(x["timestamp"]["round"])
    for team in ("teamA", "teamB"):
        out[f"kills_{team}"] = sorted(kills[team])
    out["earned_final"] = {c["team"]: 0 for c in d["champions"]}
    for c in d["champions"]:
        out["earned_final"][c["team"]] += int(c["metrics"]["earnedGold"])
    return out


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--root", default=str(ROOT / "results" / "online"))
    p.add_argument("--out", default=str(ROOT / "bots" / "main" / "foundation" / "data" / "economy" / "race_online.json"))
    a = p.parse_args()
    pool = json.loads((ROOT / "maps" / "pool.json").read_text(encoding="utf-8"))
    maps = {m["sha256"]: m["name"] for m in pool["maps"]}
    seen, games = set(), []
    for f in sorted(Path(a.root).rglob("*-summary.json*")):
        mid = f.name.split("-summary")[0]
        if mid in seen:
            continue
        seen.add(mid)
        try:
            d = load(f)
            games.append(game_row(d, maps))
        except Exception as e:  # noqa: BLE001
            print(f"skip {f.name}: {e}", file=sys.stderr)
    games = [g for g in games if g["winner"] in ("teamA", "teamB") and "bank_teamA" in g]
    n = len(games)
    ends = [g["rounds"] for g in games]
    tiebreak = [g for g in games if g["rounds"] >= 1999]
    print(f"games {n}; end round median {statistics.median(ends):.0f}, quartiles "
          f"{statistics.quantiles(ends, n=4)}; reached 2000: {len(tiebreak)} ({100 * len(tiebreak) / n:.1f}%)")
    # winner's banked lead at round r and the probability that the leader wins
    res = {"games": n, "end_round_median": statistics.median(ends), "tiebreak_games": len(tiebreak), "lead": {}, "gold_lead": {}}
    for r in (200, 300, 400, 600):
        buckets = defaultdict(lambda: [0, 0])
        gb = defaultdict(lambda: [0, 0])
        for g in games:
            if g["rounds"] < r:
                continue
            lead = g["bank_teamA"][r] - g["bank_teamB"][r]
            win = 1 if g["winner"] == "teamA" else 0
            for sgn, l, w in ((1, lead, win), (-1, -lead, 1 - win)):
                if l <= 0:
                    continue
                b = "1-49" if l < 50 else "50-149" if l < 150 else "150-299" if l < 300 else "300+"
                buckets[b][0] += w
                buckets[b][1] += 1
            gl = g["gold_teamA"].get(r, 0) - g["gold_teamB"].get(r, 0)
            for l, w in ((gl, win), (-gl, 1 - win)):
                if l <= 0:
                    continue
                b = "1-249" if l < 250 else "250-499" if l < 500 else "500-999" if l < 1000 else "1000+"
                gb[b][0] += w
                gb[b][1] += 1
        res["lead"][r] = {b: {"leader_wins": round(w / t, 3), "n": t} for b, (w, t) in sorted(buckets.items())}
        res["gold_lead"][r] = {b: {"leader_wins": round(w / t, 3), "n": t} for b, (w, t) in sorted(gb.items())}
        print(f"r{r} banked lead -> leader wins: " + ", ".join(f"{b} {w / t:.2f} (n {t})" for b, (w, t) in sorted(buckets.items())))
        print(f"r{r} gold lead   -> leader wins: " + ", ".join(f"{b} {w / t:.2f} (n {t})" for b, (w, t) in sorted(gb.items())))
    # banking rates of winners and losers (gems per round over the game)
    win_rate, lose_rate = [], []
    for g in games:
        wt = g["winner"]
        lt = "teamB" if wt == "teamA" else "teamA"
        win_rate.append(g[f"final_{wt}"] / g["rounds"])
        lose_rate.append(g[f"final_{lt}"] / g["rounds"])
    res["winner_rate_median"] = round(statistics.median(win_rate), 3)
    res["loser_rate_median"] = round(statistics.median(lose_rate), 3)
    print(f"banking rate (gems/round, whole game): winner median {res['winner_rate_median']}, loser {res['loser_rate_median']}")
    # kills: differential vs outcome
    kd = defaultdict(lambda: [0, 0])
    for g in games:
        d = len(g["kills_teamA"]) - len(g["kills_teamB"])
        win = 1 if g["winner"] == "teamA" else 0
        for l, w in ((d, win), (-d, 1 - win)):
            if l <= 0:
                continue
            b = str(min(l, 6)) if l < 6 else "6+"
            kd[b][0] += w
            kd[b][1] += 1
    res["kill_lead"] = {b: {"leader_wins": round(w / t, 3), "n": t} for b, (w, t) in sorted(kd.items())}
    print("kill lead -> leader wins: " + ", ".join(f"{b} {w / t:.2f} (n {t})" for b, (w, t) in sorted(kd.items())))
    # tiebreak games: who won on what
    tb = []
    for g in tiebreak:
        ga, gb_ = g["final_teamA"], g["final_teamB"]
        ea, eb = g["earned_final"].get("teamA", 0), g["earned_final"].get("teamB", 0)
        tb.append({"map": g["map"], "gems": [ga, gb_], "gold": [ea, eb], "winner": g["winner"],
                   "decided_by": "gems" if ga != gb_ else ("gold" if ea != eb else "kills/teamB")})
    res["tiebreak"] = tb
    by_map = defaultdict(list)
    for g in games:
        by_map[g["map"]].append(g["rounds"])
    res["end_by_map"] = {m: {"n": len(v), "median": statistics.median(v), "at2000": sum(1 for x in v if x >= 1999)} for m, v in sorted(by_map.items())}
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    Path(a.out).write_text(json.dumps(res, indent=1) + "\n", encoding="utf-8")
    print("tiebreak decided by:", {k: sum(1 for x in tb if x["decided_by"] == k) for k in ("gems", "gold", "kills/teamB")})
    return 0


if __name__ == "__main__":
    sys.exit(main())

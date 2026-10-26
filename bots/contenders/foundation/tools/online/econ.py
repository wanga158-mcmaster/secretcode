"""Economy curves, game ends and bags (online.md O-entries on economy).

usage: python -I -X utf8 econ.py
Inputs: .cache/contenders/online/sides.jsonl (sides.py) and the global ranked window lists.
Prints tables; writes data/online/econ.json.
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, ECO, OUT, US, jload, ladder, load_jsonl, map_info, mean, median, quant, tier_of

LAD = ladder()
MI = map_info()
CK = ("50", "100", "150", "200", "300", "400", "600", "800", "1000")


def band(rank):
    if rank is None:
        return None
    return "1-20" if rank <= 20 else "21-50" if rank <= 50 else "51-150" if rank <= 150 else "151-400" if rank <= 400 else "401+"


BANDS = ["1-20", "21-50", "51-150", "151-400", "401+"]


def pair_band(a, b):
    ra, rb = LAD.get(a, {}).get("rank"), LAD.get(b, {}).get("rank")
    if ra is None or rb is None:
        return None
    return band((ra + rb) / 2.0)


def game_ends():
    """End reasons by pair band in the global window."""
    c = collections.defaultdict(collections.Counter)
    for f in sorted((ECO / "lists").glob("global_*.json")):
        for m in jload(f)["matches"]:
            if m.get("status") != "finished" or not m.get("endReason"):
                continue
            b = pair_band(m["teams"][0]["teamId"], m["teams"][1]["teamId"])
            c[b][m["endReason"]] += 1
            c["all"][m["endReason"]] += 1
    return c


def pairs(rows):
    g = collections.defaultdict(dict)
    for r in rows:
        g[r["mid"]][r["team"]] = r
    return [(v["teamA"], v["teamB"]) for v in g.values() if len(v) == 2]


def main():
    rows = load_jsonl(OUT / "sides.jsonl")
    out = {}
    ends = game_ends()
    print("global window end reasons (% of finished games) by pair band (mean rank of the two teams)")
    out["ends_window"] = {}
    for b in BANDS + ["all"]:
        n = sum(ends[b].values())
        if not n:
            continue
        out["ends_window"][b] = {k: v / n for k, v in ends[b].items()} | {"n": n}
        print(f"  {b:8s} n {n:4d} " + ", ".join(f"{k} {100 * v / n:.0f}" for k, v in ends[b].most_common()))
    games = pairs(rows)
    # rounds of victory by game type
    print("\nround the game ended (gems-ended games) and share of games reaching 1000, from summaries")
    out["end_rounds"] = {}

    def gtype(a, b):
        ta, tb = a["tier"], b["tier"]
        if "US" in (ta, tb):
            o = b if ta == "US" else a
            return "us-vs-" + ("T1" if o["tier"] == "T1" else "T2" if o["tier"] == "T2" else "T3+")
        hi = min(LAD.get(a.get("tid"), {}).get("rank", 999), LAD.get(b.get("tid"), {}).get("rank", 999))
        lo = max(LAD.get(a.get("tid"), {}).get("rank", 999), LAD.get(b.get("tid"), {}).get("rank", 999))
        if lo <= 10:
            return "T1-vs-T1"
        if hi <= 10:
            return "T1-vs-other"
        return "others-" + tier_of((hi + lo) // 2)

    bytype = collections.defaultdict(list)
    for a, b in games:
        bytype[gtype(a, b)].append((a, b))
    for k in sorted(bytype):
        v = bytype[k]
        reach = [g for g in v if max(g[0]["gems_final"], g[1]["gems_final"]) >= 1000]
        rr = [g[0]["rounds"] for g in reach]
        out["end_rounds"][k] = {"games": len(v), "reach1000": len(reach) / len(v), "median_round": median(rr),
                                "p25": quant(rr, 0.25), "p75": quant(rr, 0.75)}
        print(f"  {k:14s} games {len(v):4d} reach 1000 {100 * len(reach) / len(v):3.0f}%  round median {median(rr)} "
              f"IQR {quant(rr, 0.25)}-{quant(rr, 0.75)}")
    # by map class (non-us games and us games separately)
    print("\nround of victory by map class (gems-ended), non-us games | games vs us")
    out["end_by_class"] = {}
    for cls in ("touch", "gemrich", "mid", "far"):
        res = []
        for lab, sel in (("others", lambda a, b: "US" not in (a["tier"], b["tier"])), ("us", lambda a, b: "US" in (a["tier"], b["tier"]))):
            v = [(a, b) for a, b in games if a.get("mcls") == cls and sel(a, b)]
            reach = [g for g in v if max(g[0]["gems_final"], g[1]["gems_final"]) >= 1000]
            rr = [g[0]["rounds"] for g in reach]
            out["end_by_class"][f"{cls}/{lab}"] = {"games": len(v), "reach1000": (len(reach) / len(v)) if v else None,
                                                   "median_round": median(rr)}
            res.append(f"{lab} n {len(v):3d} reach {100 * len(reach) / max(1, len(v)):3.0f}% med r{median(rr)}")
        print(f"  {cls:8s} " + " | ".join(res))
    # winner vs loser curves (non-us games only, then T1 sides vs us)
    def curves(sel_games, label):
        print(f"\n{label}: medians winner / loser (n games {len(sel_games)})")
        res = {}
        for metric in ("earnedGold", "spentGold", "collectedGems", "submittedGems", "gems", "kills"):
            line = []
            for r in CK:
                w = [ (a if a["res"] == "win" else b)[metric][r] for a, b in sel_games if a["rounds"] >= int(r)]
                l = [ (b if a["res"] == "win" else a)[metric][r] for a, b in sel_games if a["rounds"] >= int(r)]
                res.setdefault(metric, {})[r] = (median(w), median(l), len(w))
                line.append(f"{median(w):.0f}/{median(l):.0f}" if w else "-")
            print(f"  {metric:14s} " + " ".join(f"r{r}:{x}" for r, x in zip(CK, line)))
        # how often the eventual winner leads at round r
        lead = {}
        for metric in ("earnedGold", "collectedGems", "submittedGems", "kills"):
            line = []
            for r in CK:
                v = [(a, b) for a, b in sel_games if a["rounds"] >= int(r)]
                led = [((a if a["res"] == "win" else b)[metric][r], (b if a["res"] == "win" else a)[metric][r]) for a, b in v]
                led = [x for x in led if x[0] != x[1]]
                p = sum(1 for w, l in led if w > l) / len(led) if led else None
                lead.setdefault(metric, {})[r] = (p, len(led))
                line.append(f"{100 * p:.0f}%({len(led)})" if p is not None else "-")
            print(f"  winner leads {metric:14s} " + " ".join(f"r{r}:{x}" for r, x in zip(CK, line)))
        res["lead"] = lead
        return res

    others = [(a, b) for a, b in games if "US" not in (a["tier"], b["tier"]) and a["res"] != b["res"]]
    out["curves_others"] = curves(others, "games not involving us")
    t1 = [(a, b) for a, b in others if a["tier"] == "T1" and b["tier"] == "T1"]
    out["curves_t1"] = curves(t1, "T1 vs T1")
    low = [(a, b) for a, b in others if a["tier"] in ("T4", "T5") and b["tier"] in ("T4", "T5")]
    out["curves_low"] = curves(low, "T4-T5 vs T4-T5")
    # bags
    print("\ntribute bag sizes by tier (all sides with a known tier; us separately)")
    out["bags"] = {}
    for t in ("T1", "T2", "T3", "T4", "T5", "US"):
        bags = [tr[1] for r in rows if r["tier"] == t for tr in r["tributes"]]
        early = [tr[1] for r in rows if r["tier"] == t for tr in r["tributes"] if tr[0] <= 400]
        gem_in_big = sum(b for b in bags if b >= 50) / max(1, sum(bags))
        out["bags"][t] = {"n": len(bags), "median": median(bags), "p75": quant(bags, 0.75), "p90": quant(bags, 0.9),
                          "median_r400": median(early), "share_gems_in_bags_ge50": gem_in_big}
        print(f"  {t:3s} tributes {len(bags):5d} median {median(bags)} p75 {quant(bags, .75)} p90 {quant(bags, .9)} "
              f"(r<=400 median {median(early)}); gems banked in bags >= 50: {100 * gem_in_big:.0f}%")
    # carried gems at r300 and loss of carried gems to kills
    print("\ncarried gems at r300 (team) and gems lost to champion kills per game, by tier")
    out["carry"] = {}
    for t in ("T1", "T2", "T3", "T4", "T5", "US"):
        v = [r for r in rows if r["tier"] == t and r["rounds"] >= 300]
        c300 = [r["gems"]["300"] for r in v]
        lost = [r["gems_lost_champ"] for r in v]
        out["carry"][t] = {"n": len(v), "carry300_median": median(c300), "lost_champ_mean": mean(lost)}
        print(f"  {t:3s} n {len(v):4d} carried r300 median {median(c300)} mean lost to kills {mean(lost):.0f}")
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "econ.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()

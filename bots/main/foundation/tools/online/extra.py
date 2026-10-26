"""Cross-cutting tables for online.md: rating calibration, gold-vs-kill conflicts, comebacks, farming
rate per alive champion-round, early kills, our own deaths by version.

usage: python -I -X utf8 extra.py
Writes data/online/extra.json.
"""
import collections
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, OUT, expected, load_jsonl, mean, median, wilson
from drafts import window


def kb(r, R):
    return sum(1 for k in r["kills_list"] if k["r"] <= R)


def dead_rounds(r, R):
    return sum(min(60, R - d["r"]) for d in r["deaths_list"] if d["r"] <= R)


def main():
    out = {}
    # 1. rating calibration on the window
    rows = window()
    b = collections.defaultdict(lambda: [0, 0, 0.0])
    for r in rows:
        k = min(9, int(expected(r["rating"], r["orating"]) * 10))
        b[k][0] += r["win"]
        b[k][1] += 1
        b[k][2] += expected(r["rating"], r["orating"])
    out["calibration"] = {k / 10: {"n": v[1], "actual": v[0] / v[1], "fitted": v[2] / v[1]} for k, v in sorted(b.items())}
    print("rating model calibration on the window: " + "; ".join(
        f"{k / 10:.1f}: n {v[1]} act {v[0] / v[1]:.2f} fit {v[2] / v[1]:.2f}" for k, v in sorted(b.items())))
    S = load_jsonl(OUT / "sides.jsonl")
    g = collections.defaultdict(dict)
    for r in S:
        g[r["mid"]][r["team"]] = r
    G = [(v["teamA"], v["teamB"]) for v in g.values() if len(v) == 2 and v["teamA"]["res"] != v["teamB"]["res"]]
    sels = (("T1-T1", lambda a, b: a["tier"] == "T1" and b["tier"] == "T1"),
            ("others", lambda a, b: "US" not in (a["tier"], b["tier"]) and "T?" not in (a["tier"], b["tier"])),
            ("vs-us", lambda a, b: "US" in (a["tier"], b["tier"])))
    # 2. conflicts: one side ahead in earned gold, the other in kills
    print("\nconflict games (one side ahead in earned gold, the other in kills): the gold leader's wins")
    out["conflict"] = {}
    for lab, sel in sels:
        for R in (200, 400, 600):
            v = [(a, b) for a, b in G if sel(a, b) and a["rounds"] >= R]
            c = [(a, b) for a, b in v if (a["earnedGold"][str(R)] - b["earnedGold"][str(R)]) * (kb(a, R) - kb(b, R)) < 0]
            w = sum(1 for a, b in c if (a if a["earnedGold"][str(R)] > b["earnedGold"][str(R)] else b)["res"] == "win")
            lo, hi = wilson(w, len(c))
            out["conflict"][f"{lab}@{R}"] = {"games": len(v), "conflicts": len(c), "gold_leader_wins": w}
            if c:
                print(f"  {lab:7s} r{R}: {len(c):3d} of {len(v):3d} games; gold leader won {w} ({100 * w / len(c):.0f}% [{100 * lo:.0f}-{100 * hi:.0f}])")
    # 3. comebacks
    print("\nside behind in earned gold that still won")
    out["comeback"] = {}
    for lab, sel in sels:
        for R in ("200", "400", "600", "1000"):
            v = [(a, b) for a, b in G if sel(a, b) and a["rounds"] >= int(R) and a["earnedGold"][R] != b["earnedGold"][R]]
            cb = [(a, b) for a, b in v if (a if a["earnedGold"][R] < b["earnedGold"][R] else b)["res"] == "win"]
            gaps = [abs(a["earnedGold"][R] - b["earnedGold"][R]) / max(a["earnedGold"][R], b["earnedGold"][R]) for a, b in cb]
            out["comeback"][f"{lab}@{R}"] = {"games": len(v), "comebacks": len(cb), "median_deficit_share": median(gaps)}
            print(f"  {lab:7s} r{R:4s}: {len(cb)}/{len(v)} ({100 * len(cb) / max(1, len(v)):.0f}%), median deficit "
                  f"{100 * (median(gaps) or 0):.0f}% of the leader's gold")
    # 4. farming rate per alive champion-round and dead time, by tier and winners/losers
    print("\nr1-400: earned gold per alive champion-round, dead champion-rounds, collected gems per 100 alive champion-rounds")
    out["rate"] = {}
    for t in ("T1", "T2", "T3", "T4", "T5", "US"):
        v = [r for r in S if r["tier"] == t and r["rounds"] >= 400]
        x = [(r["earnedGold"]["400"] / (len(r["champs"]) * 400 - dead_rounds(r, 400)), dead_rounds(r, 400),
              100 * r["collectedGems"]["400"] / (len(r["champs"]) * 400 - dead_rounds(r, 400)),
              100 * 100 * kb(r, 400) / max(1, r["earnedGold"]["400"])) for r in v]  # kill_gold 100
        out["rate"][t] = {"n": len(v), "gold_per_alive_round": median([a[0] for a in x]), "dead_rounds": mean([a[1] for a in x]),
                          "gems_per_100": median([a[2] for a in x]), "kill_gold_share": mean([a[3] for a in x])}
        o = out["rate"][t]
        print(f"  {t:3s} n {len(v):4d} gold/alive-round {o['gold_per_alive_round']:.2f}  dead {o['dead_rounds']:.0f}  "
              f"gems/100 {o['gems_per_100']:.1f}  kill gold {o['kill_gold_share']:.0f}% of earned")
    for lab, sel in sels[:2]:
        W, L = [], []
        for a, b in G:
            if not sel(a, b) or a["rounds"] < 400:
                continue
            for r in (a, b):
                (W if r["res"] == "win" else L).append((r["earnedGold"]["400"] / (len(r["champs"]) * 400 - dead_rounds(r, 400)), dead_rounds(r, 400)))
        out["rate"][f"{lab}_win"] = {"gold_per_alive_round": median([x[0] for x in W]), "dead_rounds": mean([x[1] for x in W])}
        out["rate"][f"{lab}_loss"] = {"gold_per_alive_round": median([x[0] for x in L]), "dead_rounds": mean([x[1] for x in L])}
        print(f"  {lab}: winners {median([x[0] for x in W]):.2f} gold/alive-round, {mean([x[1] for x in W]):.0f} dead; "
              f"losers {median([x[0] for x in L]):.2f}, {mean([x[1] for x in L]):.0f} dead ({len(W)} games)")
    # 5. early kill: side with a kill by r100 (and none against) wins?
    print("\nfirst blood by r100 (one side only): its win rate")
    out["first_blood"] = {}
    for lab, sel in sels:
        v = [(a, b) for a, b in G if sel(a, b)]
        c = [(a, b) for a, b in v if (kb(a, 100) > 0) != (kb(b, 100) > 0)]
        w = sum(1 for a, b in c if (a if kb(a, 100) > 0 else b)["res"] == "win")
        lo, hi = wilson(w, len(c))
        out["first_blood"][lab] = {"games": len(v), "one_sided": len(c), "wins": w}
        print(f"  {lab:7s} {len(c)} of {len(v)} games; the side with the early kill won {100 * w / max(1, len(c)):.0f}% [{100 * lo:.0f}-{100 * hi:.0f}]")
    # 6. our deaths per game by version group and opponent tier (main's claims)
    print("\nour champion deaths per game (whole game / by r400) by version group and opponent tier")
    out["our_deaths"] = {}
    for vg, vs in (("v1-27", {str(i) for i in range(1, 28)}), ("v28-32", {"28", "29", "30", "31", "32"}), ("v33-36", {"33", "34", "35", "36"})):
        line = []
        for t in ("T1", "T2", "T3", "T4", "T5"):
            v = [r for r in S if r["tier"] == "US" and r.get("version") in vs and r.get("otier") == t]
            if len(v) < 5:
                continue
            d = mean([sum(1 for x in r["deaths_list"] if x["ctx"] == "champion") for r in v])
            d4 = mean([sum(1 for x in r["deaths_list"] if x["ctx"] == "champion" and x["r"] <= 400) for r in v])
            w = sum(r["res"] == "win" for r in v) / len(v)
            out["our_deaths"][f"{vg}/{t}"] = {"n": len(v), "deaths": d, "deaths400": d4, "win": w}
            line.append(f"{t} {d:.1f}/{d4:.1f} win {100 * w:.0f}% (n {len(v)})")
        print(f"  {vg}: " + "; ".join(line))
    # 7. which currency decides, by map class: r400 gold leader vs banked-gem leader
    print("\nr400 by map class (all games): gold leader wins | banked leader wins | conflicts (gold leader vs banked leader)")
    out["currency"] = {}
    G4 = [(a, b) for a, b in G if a["rounds"] >= 400]
    for cls in ("touch", "gemrich", "mid", "far"):
        v = [(a, b) for a, b in G4 if a.get("mcls") == cls]

        def lead_win(key):
            x = [(a, b) for a, b in v if a[key]["400"] != b[key]["400"]]
            return sum(1 for a, b in x if (a if a[key]["400"] > b[key]["400"] else b)["res"] == "win"), len(x)
        gw, gn = lead_win("earnedGold")
        sw, sn = lead_win("submittedGems")
        conf = [(a, b) for a, b in v if (a["earnedGold"]["400"] - b["earnedGold"]["400"]) *
                (a["submittedGems"]["400"] - b["submittedGems"]["400"]) < 0]
        cw = sum(1 for a, b in conf if (a if a["earnedGold"]["400"] > b["earnedGold"]["400"] else b)["res"] == "win")
        bymap = collections.defaultdict(lambda: [0, 0])
        for a, b in conf:
            bymap[a["map"]][0] += (a if a["earnedGold"]["400"] > b["earnedGold"]["400"] else b)["res"] == "win"
            bymap[a["map"]][1] += 1
        out["currency"][cls] = {"games": len(v), "gold_leader": gw / max(1, gn), "gem_leader": sw / max(1, sn),
                                "conflicts": len(conf), "gold_leader_in_conflicts": cw / max(1, len(conf)),
                                "by_map": {m: x for m, x in bymap.items()}}
        print(f"  {cls:8s} games {len(v):4d}: gold leader {100 * gw / max(1, gn):.0f}% | gem leader {100 * sw / max(1, sn):.0f}% | "
              f"conflicts {len(conf)}: gold leader won {100 * cw / max(1, len(conf)):.0f}%  "
              + ", ".join(f"{m} {x[0]}/{x[1]}" for m, x in sorted(bymap.items()) if x[1] >= 5))
    # 8. cohesion and outcome (tracked games), contact place by map class
    from style import load
    R = load()
    gg = collections.defaultdict(dict)
    for r in R:
        gg[r["mid"]][r["team"]] = r
    GT = [(v["teamA"], v["teamB"]) for v in gg.values() if len(v) == 2 and v["teamA"]["res"] != v["teamB"]["res"]
          and v["teamA"]["has_tracks"]]
    print("\ncohesion vs outcome (tracked games): winners/losers medians; more-grouped side's wins; bigger-party side's wins")
    out["cohesion"] = {}
    for lab, sel in (("T1-T1", lambda a, b: a["tier"] == "T1" and b["tier"] == "T1"),
                     ("others", lambda a, b: "US" not in (a["tier"], b["tier"]) and "T?" not in (a["tier"], b["tier"])),
                     ("vs-us", lambda a, b: "US" in (a["tier"], b["tier"]))):
        v = [(a, b) for a, b in GT if sel(a, b)]
        W = [a if a["res"] == "win" else b for a, b in v]
        L = [b if a["res"] == "win" else a for a, b in v]
        if lab == "vs-us":
            W = [x for x in W if x["tier"] != "US"]
            L = [x for x in L if x["tier"] != "US"]

        def m(lst, k):
            return median([x["f"].get(k) for x in lst])
        res = {k: (m(W, k), m(L, k)) for k in ("grouped", "solo", "spread", "party", "enemyhalf300", "recalls400")}
        pr = [(a, b) for a, b in v if a["f"].get("grouped") is not None and b["f"].get("grouped") is not None
              and abs(a["f"]["grouped"] - b["f"]["grouped"]) >= 0.1]
        wg = sum(1 for a, b in pr if (a if a["f"]["grouped"] > b["f"]["grouped"] else b)["res"] == "win")
        pp = [(a, b) for a, b in v if a["f"].get("party") and b["f"].get("party") and abs(a["f"]["party"] - b["f"]["party"]) >= 0.25]
        wp = sum(1 for a, b in pp if (a if a["f"]["party"] > b["f"]["party"] else b)["res"] == "win")
        out["cohesion"][lab] = {"games": len(v), "medians_win_loss": res, "more_grouped_wins": [wg, len(pr)],
                                "bigger_party_wins": [wp, len(pp)]}
        print(f"  {lab:7s} games {len(v):4d} " + " ".join(f"{k} {x[0]:.2f}/{x[1]:.2f}" for k, x in res.items() if x[0] is not None)
              + f"; more grouped won {wg}/{len(pr)}; bigger parties won {wp}/{len(pp)}")
    out["contact_place"] = {}
    for cls in ("touch", "gemrich", "mid", "far"):
        x = [r["f"].get("contact_frac") for r in R if r.get("mcls") == cls and r["has_tracks"] and r["team"] == "teamA"
             and r["f"].get("contact_frac") is not None]
        out["contact_place"][cls] = {"n": len(x), "median": median(x)}
        print(f"  first-contact place {cls:8s}: median {median(x):.2f} of the way between spawns (n {len(x)})")
    # 9. deaths near the victim's own spawn: when and how
    D = [d for d in load_jsonl(OUT / "deaths.jsonl") if d["mid"] in gg]
    prev = collections.defaultdict(list)
    for d in D:
        prev[(d["mid"], d["victim_team"], d["victim"])].append(d["round"])
    print("\ndeaths within 6 of the victim's own spawn ('home')")
    out["home_deaths"] = {}
    for lab, sel in (("T1 by T1", lambda v, k: v["tier"] == "T1" and k["tier"] == "T1"), ("us", lambda v, k: v["tier"] == "US"),
                     ("T2 by us", lambda v, k: v["tier"] == "T2" and k["tier"] == "US")):
        hs = []
        for d in D:
            v = gg[d["mid"]].get(d["victim_team"])
            k = gg[d["mid"]].get("teamB" if d["victim_team"] == "teamA" else "teamA")
            if not v or not k or not sel(v, k) or d["cls"] != "home":
                continue
            p = [r for r in prev[(d["mid"], d["victim_team"], d["victim"])] if r < d["round"]]
            hs.append((d["contact"], d["hp0"], d["walked_home"], bool(p) and d["round"] - max(p) <= 90, d["round"]))
        out["home_deaths"][lab] = {"n": len(hs), "contact": median([h[0] for h in hs]), "hp0": median([h[1] for h in hs]),
                                   "walked_home": median([h[2] for h in hs]),
                                   "recent_death_share": sum(h[3] for h in hs) / max(1, len(hs)), "round": median([h[4] for h in hs])}
        o = out["home_deaths"][lab]
        print(f"  {lab:9s} n {len(hs):4d} contact r{o['contact']} hp0 {o['hp0']} walked home {o['walked_home']} "
              f"died again within 90 rounds {100 * o['recent_death_share']:.0f}% round median {o['round']}")
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "extra.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()

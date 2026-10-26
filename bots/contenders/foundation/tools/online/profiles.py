"""Profiles of named teams (default: ladder #1-#3 of the 22 Oct 03:2x snapshot).

usage: python -I -X utf8 profiles.py [--team NAME ...]
Inputs: sides.jsonl, trackfeat.jsonl via style.py. Prints a profile per team; writes
data/online/profiles.json.
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, US, mean, median, quant
from style import load

DEFAULT = ["procless", "prism best girl", "Intercontinental Fish"]


def k_by(r, R):
    return sum(1 for k in r["kills_list"] if k["r"] <= R)


def profile(name, rows):
    v = [r for r in rows if r.get("name") == name]
    if not v:
        return None
    o = {}
    w = [r for r in v if r["res"] == "win"]
    o["coverage"] = {"sides": len(v), "wins": len(w), "maps": len({r["map"] for r in v}),
                     "vs": dict(collections.Counter(r["opp"] for r in v).most_common()),
                     "from": min(r["created"] for r in v), "to": max(r["created"] for r in v),
                     "tracks": sum(1 for r in v if r["has_tracks"])}
    print(f"\n=== {name}: {len(v)} sides, {len(w)} wins, {o['coverage']['maps']} maps, {o['coverage']['from'][:16]} to "
          f"{o['coverage']['to'][:16]}, tracks {o['coverage']['tracks']}")
    print("  opponents:", o["coverage"]["vs"])
    # drafts
    dr = collections.Counter(" ".join(r["draft"]) for r in v)
    o["drafts"] = dr.most_common(8)
    print("  drafts (pick order):", "; ".join(f"{k} x{n}" for k, n in dr.most_common(6)))
    bymap = collections.defaultdict(set)
    for r in v:
        bymap[r["map"]].add(" ".join(r["draft"]))
    o["drafts_by_map_varying"] = {m: sorted(s) for m, s in bymap.items() if len(s) > 1}
    print(f"  maps with more than one draft: {len(o['drafts_by_map_varying'])} of {len(bymap)}")
    # opening
    def opening(r):
        caps = r["caps"]
        return {"first_clear": r["clears"][0][0] if r["clears"] else None, "first_cap": caps[0][0] if caps else None,
                "caps100": sum(1 for c in caps if c[0] <= 100), "caps200": sum(1 for c in caps if c[0] <= 200),
                "first5": [(c[0], c[1], c[3]) for c in caps[:5]]}
    op = [opening(r) for r in v]
    o["opening"] = {"first_clear": median([x["first_clear"] for x in op]), "first_cap": median([x["first_cap"] for x in op]),
                    "caps100": mean([x["caps100"] for x in op]), "caps200": mean([x["caps200"] for x in op])}
    cat5 = collections.Counter(c[1] for x in op for c in x["first5"])
    o["opening"]["first5_types"] = dict(cat5)
    print(f"  opening: first clear r{o['opening']['first_clear']}, first capture r{o['opening']['first_cap']}, captures by r100 "
          f"{o['opening']['caps100']:.1f}, by r200 {o['opening']['caps200']:.1f}; first-5 capture types {dict(cat5.most_common())}")
    # economy vs opponent
    eco = {}
    for ck in ("100", "200", "300", "400", "600", "1000"):
        a = [r for r in v if r["rounds"] >= int(ck)]
        eco[ck] = {"gold": median([r["earnedGold"][ck] for r in a]), "ogold": median([r["opp_row"]["earnedGold"][ck] for r in a]),
                   "banked": median([r["submittedGems"][ck] for r in a]), "obanked": median([r["opp_row"]["submittedGems"][ck] for r in a]),
                   "collected": median([r["collectedGems"][ck] for r in a]), "n": len(a)}
    o["economy"] = eco
    print("  earned gold (own/opp), banked gems (own/opp) medians: " + "; ".join(
        f"r{ck} {e['gold']:.0f}/{e['ogold']:.0f}, {e['banked']:.0f}/{e['obanked']:.0f}" for ck, e in eco.items() if e["n"]))
    wins_rounds = [r["rounds"] for r in w if r["gems_final"] >= 1000]
    o["win_rounds"] = {"median": median(wins_rounds), "p25": quant(wins_rounds, .25), "p75": quant(wins_rounds, .75)}
    print(f"  wins by 1000 gems at round median {median(wins_rounds)} [{quant(wins_rounds, .25)}-{quant(wins_rounds, .75)}]")
    # banking
    bags = [t[1] for r in v for t in r["tributes"]]
    big = [(r["mid"][:8], r["map"], t[0], t[1], next((c["name"] for c in r["champs"] if c["id"] == t[2]), "?"))
           for r in v for t in r["tributes"] if t[1] >= 200]
    o["banking"] = {"tributes_per_game": len(bags) / len(v), "bag_median": median(bags), "bag_p90": quant(bags, .9),
                    "big_tributes": big[:12], "locks_per_game": mean([r["f"]["locks"] for r in v]),
                    "carry_by_champ": {}}
    carriers = collections.defaultdict(list)
    for r in v:
        for c in r["champs"]:
            carriers[c["name"]].append(c["maxcarry"])
    o["banking"]["carry_by_champ"] = {k: median(x) for k, x in carriers.items()}
    print(f"  banking: {len(bags) / len(v):.1f} tributes a game, bag median {median(bags)} p90 {quant(bags, .9)}; locks {o['banking']['locks_per_game']:.1f} a game; "
          f"median max carry by champion {o['banking']['carry_by_champ']}")
    if big:
        print("  tributes of 200+ gems:", big[:8])
    # shopping
    fb = collections.defaultdict(list)
    for r in v:
        for c in r["champs"]:
            fb[c["name"]].append(c["buys"][0][0] if c["buys"] else None)
    o["first_buy"] = {k: median(x) for k, x in fb.items()}
    print("  first purchase by champion (median round):", {k: x for k, x in o["first_buy"].items()})
    # fights
    fd = [r["first_cdmg"] for r in v]
    o["fights"] = {"first_own_dmg": median(fd), "kills400": mean([k_by(r, 400) for r in v]),
                   "deaths400": mean([k_by(r["opp_row"], 400) for r in v]), "kills": mean([len(r["kills_list"]) for r in v]),
                   "deaths": mean([len(r["opp_row"]["kills_list"]) for r in v]),
                   "camp_deaths": mean([sum(1 for d in r["deaths_list"] if d["ctx"] == "camp") for r in v])}
    killers = collections.Counter(k["killer"] for r in v for k in r["kills_list"])
    victims = collections.Counter(d["victim"] for r in v for d in r["deaths_list"] if d["ctx"] == "champion")
    o["fights"]["killers"] = dict(killers.most_common())
    o["fights"]["victims"] = dict(victims.most_common())
    print(f"  fights: own first champion damage median r{median(fd)}; K/D by r400 {o['fights']['kills400']:.1f}/{o['fights']['deaths400']:.1f}, "
          f"whole game {o['fights']['kills']:.1f}/{o['fights']['deaths']:.1f}; camp deaths {o['fights']['camp_deaths']:.1f}")
    print(f"  killers {dict(killers.most_common())}; their dead {dict(victims.most_common())}")
    # cohesion
    t = [r["f"] for r in v if r["has_tracks"]]
    if t:
        o["movement"] = {k: median([x.get(k) for x in t]) for k in ("grouped", "solo", "spread", "enemyhalf", "spawn", "recalls400",
                                                                     "party", "ranged_hold", "contact_frac")}
        print("  movement (medians, n %d): " % len(t) + ", ".join(f"{k} {x:.2f}" for k, x in o["movement"].items() if x is not None))
    # losses
    o["losses"] = []
    for r in sorted([r for r in v if r["res"] == "loss"], key=lambda r: r["created"]):
        op_ = r["opp_row"]
        o["losses"].append({"mid": r["mid"][:8], "map": r["map"], "opp": r["opp"], "rounds": r["rounds"],
                            "gems": f"{r['gems_final']}-{r['ogems_final']}",
                            "gold400": f"{r['earnedGold']['400']:.0f}-{op_['earnedGold']['400']:.0f}",
                            "kd400": f"{k_by(r, 400)}-{k_by(op_, 400)}", "kd": f"{len(r['kills_list'])}-{len(op_['kills_list'])}",
                            "opp_draft": " ".join(op_["draft"])})
    print("  losses:")
    for x in o["losses"]:
        print(f"    {x['mid']} {x['map']:20s} vs {x['opp'][:18]:18s} r{x['rounds']} gems {x['gems']} gold400 {x['gold400']} "
              f"K-D400 {x['kd400']} K-D {x['kd']} | opp {x['opp_draft']}")
    return o


def main():
    names = DEFAULT
    if "--team" in sys.argv:
        names = sys.argv[sys.argv.index("--team") + 1:]
    rows = load()
    games = collections.defaultdict(dict)
    for r in rows:
        games[r["mid"]][r["team"]] = r
    for g in games.values():
        if len(g) == 2:
            g["teamA"]["opp_row"], g["teamB"]["opp_row"] = g["teamB"], g["teamA"]
    rows = [r for r in rows if r.get("opp_row")]
    out = {n: profile(n, rows) for n in names}
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "profiles.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1, ensure_ascii=False)


if __name__ == "__main__":
    main()

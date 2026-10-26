"""Strategy catalog: prevalence of each style flag by rank tier, its record, and style head-to-heads.

usage: python -I -X utf8 catalog.py
Inputs: sides.jsonl, trackfeat.jsonl (style.py). Prints tables; writes data/online/catalog.json.
Records: (a) games not involving us, as win% and residual vs the rating expectation from the
22 Oct 03:2x ladder snapshot; (b) games against our recent versions (v33-v35, mostly the survey),
a common opponent: win% with the flag vs without, within tier.
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, US, expected, mean, median, quant, wilson
from style import FLAGS, load

TIERS = ("T1", "T2", "T3", "T4", "T5")




def main():
    rows = load()
    games = collections.defaultdict(dict)
    for r in rows:
        games[r["mid"]][r["team"]] = r
    for g in games.values():
        if len(g) == 2:
            g["teamA"]["opp_row"], g["teamB"]["opp_row"] = g["teamB"], g["teamA"]
    teams = [r for r in rows if r["tier"] in TIERS]
    others = [r for r in teams if r.get("otier") in TIERS and r.get("rating") and r.get("orating")]
    vsus = [r for r in teams if r.get("otier") == "US" and r.get("version") in ("33", "34", "35", "36")]
    out = {"coverage": {"team_sides": len(teams), "non_us_sides": len(others), "vs_us_v33_36_sides": len(vsus),
                        "with_tracks": sum(1 for r in teams if r["has_tracks"])}}
    print(f"team sides {len(teams)} (non-us {len(others)}, vs us v33-36 {len(vsus)}; with tracks "
          f"{sum(1 for r in teams if r['has_tracks'])})")
    print("sides per tier:", {t: sum(1 for r in teams if r["tier"] == t) for t in TIERS},
          "with tracks:", {t: sum(1 for r in teams if r["tier"] == t and r["has_tracks"]) for t in TIERS})
    # 1. prevalence
    print("\nflag prevalence % by tier (sides where the flag is measurable; n in brackets for the track flags)")
    print(f"{'flag':15s} " + " ".join(f"{t:>9s}" for t in TIERS) + "      US")
    out["prevalence"] = {}
    us = [r for r in rows if r["tier"] == "US"]
    for k in FLAGS:
        line = []
        for t in TIERS + ("US",):
            v = [r["flags"][k] for r in (teams if t != "US" else us) if (r["tier"] == t) and r["flags"][k] is not None]
            p = sum(v) / len(v) if v else None
            out["prevalence"].setdefault(k, {})[t] = {"p": p, "n": len(v)}
            line.append(f"{100 * p:4.0f}({len(v):3d})" if p is not None else f"{'-':>9s}")
        print(f"{k:15s} " + " ".join(f"{x:>9s}" for x in line))
    # 2. record in games not involving us
    print("\nrecord in games not involving us: sides with flag n, win%, residual vs rating (points); without flag residual")
    out["record_others"] = {}
    for k in FLAGS:
        if FLAGS[k][2] != "choice":
            continue
        w = [r for r in others if r["flags"][k]]
        wo = [r for r in others if r["flags"][k] is False]
        if len(w) < 8:
            continue
        res = mean([(r["res"] == "win") - expected(r["rating"], r["orating"]) for r in w])
        res0 = mean([(r["res"] == "win") - expected(r["rating"], r["orating"]) for r in wo])
        wr = sum(r["res"] == "win" for r in w) / len(w)
        out["record_others"][k] = {"n": len(w), "win": wr, "resid": res, "resid_without": res0, "n_without": len(wo)}
        print(f"  {k:15s} n {len(w):4d} win {100 * wr:3.0f}% resid {100 * res:+5.1f}  | without n {len(wo):4d} resid {100 * res0:+5.1f}")
    # 3. record against us (common opponent), within tier
    print("\nwin% against our v33-v36 (common opponent), with flag / without flag, by tier (n)")
    out["record_vs_us"] = {}
    for k in FLAGS:
        if FLAGS[k][2] != "choice":
            continue
        line, any_ = [], False
        for t in TIERS:
            w = [r for r in vsus if r["tier"] == t and r["flags"][k]]
            wo = [r for r in vsus if r["tier"] == t and r["flags"][k] is False]
            if len(w) >= 5 and len(wo) >= 5:
                any_ = True
                a = sum(r["res"] == "win" for r in w) / len(w)
                b = sum(r["res"] == "win" for r in wo) / len(wo)
                out["record_vs_us"].setdefault(k, {})[t] = {"with": a, "n_with": len(w), "without": b, "n_without": len(wo)}
                line.append(f"{t} {100 * a:3.0f}/{100 * b:3.0f} ({len(w)}/{len(wo)})")
        if any_:
            print(f"  {k:15s} " + "  ".join(line))
    # 4. head-to-head on opposed style pairs, all games (non-us), side with A vs side with B (A and B exclusive)
    print("\nhead-to-head, games not involving us: side with A (and not B) vs side with B (and not A); win% of A, residual")
    pairs = [("grouped", "dispersed"), ("contact_seeker", "contact_avoider"), ("health_build", "strength_build"),
             ("focus_build", "strength_build"), ("focus_build", "health_build"), ("gem_rush", "monster_farm"),
             ("big_bags", "lock_user"), ("lock_user", "no_locks"), ("invader", "turtle"), ("early_shop", "treasure_first"),
             ("meta_draft", "template_draft"), ("contact_seeker", "dispersed"), ("grouped", "contact_seeker"),
             ("early_siege", "contact_seeker"), ("crystal_routes", "contact_seeker")]
    out["h2h"] = {}
    for a, b in pairs:
        v = [r for r in others if r["flags"][a] and not r["flags"][b] and r.get("opp_row")
             and r["opp_row"]["flags"][b] and not r["opp_row"]["flags"][a]]
        if len(v) < 6:
            continue
        wr = sum(r["res"] == "win" for r in v) / len(v)
        res = mean([(r["res"] == "win") - expected(r["rating"], r["orating"]) for r in v])
        lo, hi = wilson(sum(r["res"] == "win" for r in v), len(v))
        out["h2h"][f"{a}>{b}"] = {"n": len(v), "win": wr, "resid": res}
        print(f"  {a:14s} vs {b:14s} n {len(v):3d} win {100 * wr:3.0f}% [{100 * lo:.0f}-{100 * hi:.0f}] resid {100 * res:+.1f}")
    # 5. feature medians by tier (the numbers behind the flags)
    print("\nfeature medians by tier: grouped share, solo share, spread, enemyhalf, spawn, recalls<=400/champ, party size at damage, "
          "first own dmg, kills400, H/S/F, first buy, gem caps share, bag median, locks, crystals")
    out["feature_medians"] = {}
    keys = ("grouped", "solo", "spread", "enemyhalf300", "spawn", "recalls400", "party", "first_dmg", "contact_rel", "kills400",
            "H", "S", "F", "first_buy", "gem_caps200", "bag_med400", "locks", "crystals400", "maxcarry", "camp_deaths400",
            "ranged_hold")
    for t in TIERS + ("US",):
        v = [r["f"] for r in (teams if t != "US" else us) if r["tier"] == t]
        med = {k: median([x.get(k) for x in v]) for k in keys}
        out["feature_medians"][t] = med
        print(f"  {t:3s} " + " ".join(f"{k}={med[k]:.2f}" if isinstance(med[k], float) else f"{k}={med[k]}" for k in keys))
    # 6. example teams per flag: teams with >= 3 measurable sides showing the flag in >= 50% of them
    print("\nexample teams per flag (>= 3 sides, flag in >= 50%): #rank name share/sides, example game")
    out["examples"] = {}
    for k in FLAGS:
        by = collections.defaultdict(list)
        for r in teams:
            if r["flags"][k] is not None:
                by[(r["rank"], r["name"])].append(r)
        ex = []
        for (rank, name), v in sorted(by.items(), key=lambda kv: kv[0][0] or 999):
            if len(v) < 3:
                continue
            p = sum(1 for r in v if r["flags"][k]) / len(v)
            if p >= 0.5:
                e = next(r for r in v if r["flags"][k])
                ex.append({"rank": rank, "team": name, "share": p, "sides": len(v), "mid": e["mid"][:8], "map": e["map"]})
        out["examples"][k] = ex[:6]
        print(f"  {k:15s} " + "; ".join(f"#{e['rank']} {e['team'][:18]} {100 * e['share']:.0f}%/{e['sides']} {e['mid']} {e['map']}"
                                        for e in ex[:5]))
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "catalog.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()

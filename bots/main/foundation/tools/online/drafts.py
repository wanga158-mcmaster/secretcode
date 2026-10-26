"""Draft statistics of the whole ladder from the global ranked window (read-only, local lists).

usage: python -I -X utf8 drafts.py
Input: .cache/main/analysis/ecosystem/lists/global_*.json (every ranked game created 22 Oct
03:10-03:20 UTC) and the ladder snapshot read right after it; plus every other match list and our
registry for the per-(team, map) draft stability test. Output: printed tables and
data/online/drafts.json.
"""
import collections
import itertools
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, ECO, PICKS, expected, jload, ladder, map_info, match_meta, tier_of, wilson

LAD = ladder()
MI = map_info()


def band(rank):
    if rank is None:
        return None
    if rank <= 20:
        return "1-20"
    if rank <= 50:
        return "21-50"
    if rank <= 150:
        return "51-150"
    if rank <= 400:
        return "151-400"
    return "401+"


BANDS = ["1-20", "21-50", "51-150", "151-400", "401+"]




def window():
    rows = []
    for f in sorted((ECO / "lists").glob("global_*.json")):
        for m in jload(f)["matches"]:
            if m.get("status") != "finished" or any(t.get("result") not in ("win", "loss") for t in m["teams"]):
                continue
            if any(not t.get("draftPicks") for t in m["teams"]):
                continue
            a, b = m["teams"]
            for me, op in ((a, b), (b, a)):
                lm, lo = LAD.get(me["teamId"]), LAD.get(op["teamId"])
                if not lm or not lo:
                    continue
                rows.append({"mid": m["matchId"], "map": m["map"], "end": m["endReason"], "tid": me["teamId"],
                             "name": me["name"], "rank": lm["rank"], "rating": lm["rating"], "orank": lo["rank"],
                             "orating": lo["rating"], "picks": [PICKS[i] for i in me["draftPicks"]],
                             "opicks": [PICKS[i] for i in op["draftPicks"]], "win": me["result"] == "win",
                             "side": me["side"], "exp": expected(lm["rating"], lo["rating"])})
    return rows


def pct(x):
    return f"{100 * x:.0f}" if x is not None else "-"


def main():
    rows = window()
    games = len({r["mid"] for r in rows})
    teams = len({r["tid"] for r in rows})
    print(f"global window: {games} games, {len(rows)} team-games, {teams} teams; engine 1.0.2")
    by = collections.defaultdict(list)
    for r in rows:
        by[band(r["rank"])].append(r)
    out = {"coverage": {"games": games, "team_games": len(rows), "teams": teams,
                        "window": "ranked games created 2026-10-22 03:10-03:20 UTC"}}
    # 1. pick rates by band
    print("\npick rate % by rank band (team-games n):", {b: len(by[b]) for b in BANDS})
    print(f"{'champ':9s} " + " ".join(f"{b:>8s}" for b in BANDS))
    out["pick_rate"] = {}
    for c in PICKS:
        vals = [sum(1 for r in by[b] if c in r["picks"]) / max(1, len(by[b])) for b in BANDS]
        out["pick_rate"][c] = dict(zip(BANDS, vals))
        print(f"{c:9s} " + " ".join(f"{pct(v):>8s}" for v in vals))
    # 2. win rate and residual vs rating when picked (top 20 vs rest), and the within-game contrast
    print("\nwin% / residual (points vs rating expectation) when picked; n")
    out["value"] = {}
    for grp, sel in (("top20", lambda r: r["rank"] <= 20), ("rest", lambda r: r["rank"] > 20), ("all", lambda r: True)):
        print(f"[{grp}]")
        for c in PICKS:
            w = [r for r in rows if sel(r) and c in r["picks"]]
            wo = [r for r in rows if sel(r) and c not in r["picks"]]
            if len(w) < 10:
                continue
            res_w = sum(r["win"] - r["exp"] for r in w) / len(w)
            res_wo = sum(r["win"] - r["exp"] for r in wo) / max(1, len(wo))
            # within-game: X on this side only
            xg = [r for r in w if c not in r["opicks"]]
            res_x = sum(r["win"] - r["exp"] for r in xg) / max(1, len(xg))
            out["value"].setdefault(grp, {})[c] = {"n": len(w), "win": sum(r["win"] for r in w) / len(w),
                                                   "resid": res_w, "resid_without": res_wo,
                                                   "n_only_side": len(xg), "resid_only_side": res_x}
            print(f"  {c:9s} n {len(w):4d} win {pct(sum(r['win'] for r in w) / len(w)):>3s}% resid {100 * res_w:+5.1f}"
                  f" (without {100 * res_wo:+5.1f}); only this side has it: n {len(xg):4d} resid {100 * res_x:+5.1f}")
    # 3. first pick and mean position by band
    print("\nfirst pick % by band")
    out["first_pick"] = {}
    for b in BANDS:
        fp = collections.Counter(r["picks"][0] for r in by[b])
        out["first_pick"][b] = {k: v / len(by[b]) for k, v in fp.items()}
        print(f"  {b:8s} " + ", ".join(f"{k} {pct(v / len(by[b]))}" for k, v in fp.most_common(5)))
    print("\nmean pick position (1-based) in top 20, given picked")
    out["position_top20"] = {}
    t20 = [r for r in rows if r["rank"] <= 20]
    for c in PICKS:
        pos = [r["picks"].index(c) + 1 for r in t20 if c in r["picks"]]
        if pos:
            out["position_top20"][c] = sum(pos) / len(pos)
    print("  " + ", ".join(f"{c} {v:.1f}" for c, v in sorted(out["position_top20"].items(), key=lambda kv: kv[1])))
    # 4. comps (sets) on 5-pick maps, top 20 and all
    print("\nmost common 5-champion comps (5-pick maps): n, win%, residual")
    out["comps"] = {}
    for grp, sel in (("top20", lambda r: r["rank"] <= 20), ("21-150", lambda r: 20 < r["rank"] <= 150),
                     ("151+", lambda r: r["rank"] > 150)):
        cc = collections.defaultdict(list)
        for r in rows:
            if sel(r) and len(r["picks"]) == 5:
                cc[tuple(sorted(r["picks"]))].append(r)
        tot = sum(len(v) for v in cc.values())
        print(f"[{grp}] {tot} team-games, {len(cc)} distinct comps")
        out["comps"][grp] = []
        for k, v in sorted(cc.items(), key=lambda kv: -len(kv[1]))[:6]:
            res = sum(r["win"] - r["exp"] for r in v) / len(v)
            out["comps"][grp].append({"comp": k, "n": len(v), "win": sum(r["win"] for r in v) / len(v), "resid": res,
                                      "teams": len({r["tid"] for r in v})})
            print(f"  {'+'.join(k):45s} n {len(v):4d} teams {len({r['tid'] for r in v}):3d} win {pct(sum(r['win'] for r in v) / len(v))}% resid {100 * res:+.1f}")
    # 5. map dependence in the top 20: pick rate by draft size and map class
    print("\ntop-20 pick rate % by map draft size and class")
    out["map_dep"] = {}
    keys = sorted({(MI[r['map']]['draft'], MI[r['map']]['cls']) for r in t20 if r["map"] in MI})
    hdr = "  " + "champ     " + " ".join(f"{d}/{c:7s}" for d, c in keys)
    print(hdr)
    for c in PICKS:
        vals = []
        for k in keys:
            sub = [r for r in t20 if r["map"] in MI and (MI[r['map']]['draft'], MI[r['map']]['cls']) == k]
            vals.append(sum(1 for r in sub if c in r["picks"]) / max(1, len(sub)))
        out["map_dep"][c] = {f"{d}/{cl}": v for (d, cl), v in zip(keys, vals)}
        print(f"  {c:9s} " + " ".join(f"{pct(v):>9s}" for v in vals))
    print("  n: " + " ".join(f"{sum(1 for r in t20 if r['map'] in MI and (MI[r['map']]['draft'], MI[r['map']]['cls']) == k):>9d}" for k in keys))
    # 6. draft families by band and their head-to-head
    def fam(p):
        s = set(p)
        meta = len(s & {"Ryth", "Vesper", "Vendetta", "Marcella", "Heart"})
        tmpl = len(s & {"C&I", "Stellar", "Ren", "Varg"})
        if meta >= 3 and ("Heart" in s or "Marcella" in s):
            return "meta"
        if tmpl >= 3:
            return "template"
        if "Prism" in s:
            return "prism"
        if "Willow" in s:
            return "willow"
        return "other"
    print("\ndraft family share % by band")
    out["families"] = {}
    for b in BANDS:
        fc = collections.Counter(fam(r["picks"]) for r in by[b])
        out["families"][b] = {k: v / len(by[b]) for k, v in fc.items()}
        print(f"  {b:8s} " + ", ".join(f"{k} {pct(v / len(by[b]))}" for k, v in fc.most_common()))
    print("\nfamily head-to-head (row side): win%, residual, n (different families only)")
    h2h = collections.defaultdict(list)
    for r in rows:
        fa, fo = fam(r["picks"]), fam(r["opicks"])
        if fa != fo:
            h2h[(fa, fo)].append(r)
    out["fam_h2h"] = {}
    for (fa, fo), v in sorted(h2h.items(), key=lambda kv: -len(kv[1])):
        if len(v) < 20 or fa > fo:
            continue
        res = sum(r["win"] - r["exp"] for r in v) / len(v)
        lo, hi = wilson(sum(r["win"] for r in v), len(v))
        out["fam_h2h"][f"{fa}>{fo}"] = {"n": len(v), "win": sum(r["win"] for r in v) / len(v), "resid": res}
        print(f"  {fa:8s} vs {fo:8s} n {len(v):4d} win {pct(sum(r['win'] for r in v) / len(v))}% [{pct(lo)}-{pct(hi)}] resid {100 * res:+.1f}")
    # 7. stability: does a team draft the same on the same map? (all lists held)
    meta = match_meta()
    tm = collections.defaultdict(list)
    for mid, m in meta.items():
        for t in m["teams"]:
            if t.get("draftPicks"):
                o = [x for x in m["teams"] if x is not t][0]
                tm[(t["teamId"], m["map"])].append((tuple(t["draftPicks"]), tuple(o.get("draftPicks") or ()), m["createdAt"]))
    stab = collections.defaultdict(lambda: [0, 0])
    for (tid, mp), v in tm.items():
        if len(v) < 2:
            continue
        r = LAD.get(tid, {}).get("rank")
        b = band(r)
        same = len({x[0] for x in v}) == 1
        stab[b][0] += same
        stab[b][1] += 1
    print("\nsame (team, map) drafted identically across all held games: share of (team, map) pairs with >= 2 games")
    out["stability"] = {}
    for b in BANDS:
        if stab[b][1]:
            out["stability"][b] = {"identical": stab[b][0], "pairs": stab[b][1]}
            print(f"  {b:8s} {stab[b][0]}/{stab[b][1]} ({pct(stab[b][0] / stab[b][1])}%)")
    # 8. reaction: when a top-20 team's draft varies on one map, does pick k follow the opponent's picks < k?
    react = collections.Counter()
    for (tid, mp), v in tm.items():
        r = LAD.get(tid, {}).get("rank")
        if r is None or r > 20 or len(v) < 2:
            continue
        drafts = {x[0] for x in v}
        if len(drafts) == 1:
            react["fixed"] += 1
            continue
        # first position where drafts differ; were the opponents' earlier picks different there?
        k = min(i for i in range(5) if len({d[i] if i < len(d) else None for d in drafts}) > 1)
        prior = {x[1][:k] for x in v}
        react["varies_after_same_opp_prefix" if len(prior) == 1 else "varies_with_opp_prefix"] += 1
    out["reaction_top20"] = dict(react)
    print("\ntop-20 (team, map) pairs with >= 2 games:", dict(react))
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "drafts.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()

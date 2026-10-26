"""Fights from summaries: first contact, kills by round, kill places, and what decided each game.

usage: python -I -X utf8 fights.py
Input: .cache/contenders/online/sides.jsonl. Prints tables; writes data/online/fights.json.
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, OUT, load_jsonl, mean, median, quant, wilson


def games(rows):
    g = collections.defaultdict(dict)
    for r in rows:
        g[r["mid"]][r["team"]] = r
    return [(v["teamA"], v["teamB"]) for v in g.values() if len(v) == 2 and v["teamA"]["res"] != v["teamB"]["res"]]


def gtype(a, b):
    ts = {a["tier"], b["tier"]}
    if "US" in ts:
        o = b if a["tier"] == "US" else a
        return "vs-us:" + ("T1" if o["tier"] == "T1" else "T2" if o["tier"] == "T2" else "T3+")
    if ts == {"T1"}:
        return "T1-T1"
    if "T1" in ts:
        return "T1-other"
    if ts <= {"T2", "T3"}:
        return "T2-T3"
    return "T4-T5"


def kills_by(r, R):
    return sum(1 for k in r["kills_list"] if k["r"] <= R)


def main():
    rows = load_jsonl(OUT / "sides.jsonl")
    G = games(rows)
    out = {}
    types = sorted({gtype(a, b) for a, b in G})
    # 1. first contact (first champion damage by either team) and first kill
    print("first champion damage in the game (min over teams) and first kill, medians [IQR]; by game type")
    out["first_contact"] = {}
    for t in types:
        v = [(a, b) for a, b in G if gtype(a, b) == t]
        fc = [min(x for x in (a["first_cdmg"], b["first_cdmg"]) if x is not None) for a, b in v
              if a["first_cdmg"] is not None or b["first_cdmg"] is not None]
        fk = [min([k["r"] for k in a["kills_list"] + b["kills_list"]], default=None) for a, b in v]
        nok = sum(1 for x in fk if x is None)
        out["first_contact"][t] = {"games": len(v), "first_dmg_median": median(fc), "q25": quant(fc, .25),
                                   "q75": quant(fc, .75), "first_kill_median": median(fk), "no_kill_share": nok / len(v)}
        print(f"  {t:10s} games {len(v):4d} first dmg r{median(fc)} [{quant(fc, .25)}-{quant(fc, .75)}]  first kill r{median(fk)} "
              f"[{quant(fk, .25)}-{quant(fk, .75)}]  games without a kill {100 * nok / len(v):.0f}%")
    # by map class (others only)
    print("\nfirst champion damage by map class (games not involving us | games vs us)")
    out["first_contact_cls"] = {}
    for cls in ("touch", "gemrich", "mid", "far"):
        parts = []
        for lab, sel in (("others", lambda a, b: "US" not in (a["tier"], b["tier"])), ("us", lambda a, b: "US" in (a["tier"], b["tier"]))):
            v = [(a, b) for a, b in G if a.get("mcls") == cls and sel(a, b)]
            fc = [min(x for x in (a["first_cdmg"], b["first_cdmg"]) if x is not None) for a, b in v
                  if a["first_cdmg"] is not None or b["first_cdmg"] is not None]
            out["first_contact_cls"][f"{cls}/{lab}"] = {"games": len(v), "median": median(fc)}
            parts.append(f"{lab} n {len(v):3d} r{median(fc)} [{quant(fc, .25)}-{quant(fc, .75)}]")
        print(f"  {cls:8s} " + " | ".join(parts))
    # 2. kills per game by round (both teams), by type
    print("\nkills per game (both teams, mean) by round")
    out["kills_by_round"] = {}
    for t in types:
        v = [(a, b) for a, b in G if gtype(a, b) == t]
        line = []
        for R in (100, 200, 400, 600, 1000, 2000):
            k = [kills_by(a, R) + kills_by(b, R) for a, b in v]
            out["kills_by_round"].setdefault(t, {})[R] = mean(k)
            line.append(f"r{R} {mean(k):.1f}")
        print(f"  {t:10s} " + "  ".join(line))
    # 3. who wins: kill differential vs gold differential at r400 (games reaching r400)
    print("\nwin rate of the side ahead at r400, by what it is ahead in (games not involving us)")
    oth = [(a, b) for a, b in G if "US" not in (a["tier"], b["tier"]) and a["rounds"] >= 400]
    out["ahead400"] = {}
    for lab, f in (("kills", lambda r: kills_by(r, 400)), ("gold", lambda r: r["earnedGold"]["400"]),
                   ("banked", lambda r: r["submittedGems"]["400"]), ("collected", lambda r: r["collectedGems"]["400"])):
        win = n = 0
        for a, b in oth:
            fa, fb = f(a), f(b)
            if fa == fb:
                continue
            lead = a if fa > fb else b
            n += 1
            win += lead["res"] == "win"
        lo, hi = wilson(win, n)
        out["ahead400"][lab] = {"n": n, "win": win / n if n else None}
        print(f"  ahead in {lab:9s} n {n:3d} wins {100 * win / max(1, n):.0f}% [{100 * lo:.0f}-{100 * hi:.0f}]")
    # kill differential bins
    print("\nwin rate by kill differential at r400 (games not involving us)")
    bins = collections.defaultdict(lambda: [0, 0])
    for a, b in oth:
        d = kills_by(a, 400) - kills_by(b, 400)
        d = max(-3, min(3, d))
        bins[d][0] += a["res"] == "win"
        bins[d][1] += 1
    out["killdiff400"] = {d: bins[d] for d in sorted(bins)}
    print("  " + "  ".join(f"{d:+d}: {100 * bins[d][0] / bins[d][1]:.0f}% ({bins[d][1]})" for d in sorted(bins)))
    # 4. economy-decided vs fight-decided (whole game): winner kills vs loser kills
    print("\nwhat decided the game (whole-game kills of winner vs loser); econ = winner had fewer kills; by type and map class")
    out["decided"] = {}

    def cls_of(a, b):
        w = a if a["res"] == "win" else b
        l = b if w is a else a
        kw, kl = len(w["kills_list"]), len(l["kills_list"])
        if kw > kl:
            return "fight"
        if kw < kl:
            return "econ"
        return "even"

    for key, sel in [(t, (lambda t: lambda a, b: gtype(a, b) == t)(t)) for t in types] + \
                    [(f"cls:{c}", (lambda c: lambda a, b: a.get("mcls") == c and "US" not in (a["tier"], b["tier"]))(c))
                     for c in ("touch", "gemrich", "mid", "far")]:
        v = [(a, b) for a, b in G if sel(a, b)]
        if not v:
            continue
        c = collections.Counter(cls_of(a, b) for a, b in v)
        n = len(v)
        out["decided"][key] = {k: c[k] / n for k in ("fight", "econ", "even")} | {"n": n}
        print(f"  {key:14s} n {n:4d} fight-decided {100 * c['fight'] / n:3.0f}%  econ-decided {100 * c['econ'] / n:3.0f}%  even kills {100 * c['even'] / n:3.0f}%")
    # 5. kill places: near own spawn (<=6), near enemy spawn (<=6), elsewhere; and context of deaths (camp vs champion)
    print("\ndeath places and causes per side-game (mean): champion kills near the victim's own spawn / near the killer's spawn / elsewhere; camp deaths")
    out["deaths"] = {}
    for t in ("T1", "T2", "T3", "T4", "T5", "US"):
        v = [r for r in rows if r["tier"] == t]
        own = mean([sum(1 for d in r["deaths_list"] if d["ctx"] == "champion" and d["own_spawn_d"] is not None and d["own_spawn_d"] <= 6) for r in v])
        opp = mean([sum(1 for d in r["deaths_list"] if d["ctx"] == "champion" and d["opp_spawn_d"] is not None and d["opp_spawn_d"] <= 6) for r in v])
        tot = mean([sum(1 for d in r["deaths_list"] if d["ctx"] == "champion") for r in v])
        camp = mean([sum(1 for d in r["deaths_list"] if d["ctx"] == "camp") for r in v])
        camp400 = mean([sum(1 for d in r["deaths_list"] if d["ctx"] == "camp" and d["r"] <= 400) for r in v])
        out["deaths"][t] = {"n": len(v), "champ": tot, "near_own_spawn": own, "near_enemy_spawn": opp, "camp": camp, "camp400": camp400}
        print(f"  {t:3s} n {len(v):4d} champion deaths {tot:.1f} (own spawn {own:.1f}, enemy spawn {opp:.1f}); camp deaths {camp:.1f} (r<=400 {camp400:.1f})")
    # 6. kills by champion type: share of team kills per champion, T1 and others
    print("\nkill share by champion type (whole game), T1 sides vs T2-T5 sides; deaths share")
    out["killers"] = {}
    for lab, sel in (("T1", lambda r: r["tier"] == "T1"), ("T2-T5", lambda r: r["tier"] in ("T2", "T3", "T4", "T5"))):
        kc, dc, pc = collections.Counter(), collections.Counter(), collections.Counter()
        for r in rows:
            if not sel(r):
                continue
            for c in r["champs"]:
                kc[c["name"]] += c["kills"]
                dc[c["name"]] += c["deaths"]
                pc[c["name"]] += 1
        tk, td = sum(kc.values()), sum(dc.values())
        out["killers"][lab] = {ch: {"kills_per_game": kc[ch] / pc[ch], "deaths_per_game": dc[ch] / pc[ch], "games": pc[ch]} for ch in pc}
        print(f"  [{lab}] " + ", ".join(f"{ch} {kc[ch] / pc[ch]:.1f}/{dc[ch] / pc[ch]:.1f}" for ch, _ in pc.most_common()) + "   (kills/deaths per champion-game)")
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "fights.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()

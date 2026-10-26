"""Tables over deaths.jsonl and episodes.jsonl (deathctx.py): kill mechanisms and contact outcomes by tier.

usage: python -I -X utf8 deathtab.py
Writes data/online/deaths.json.
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import DATA, OUT, load_jsonl, mean, median, quant

CLS = ("camp", "home", "burst", "chase", "picked", "outnumbered", "duel", "brawl")
TIERS = ("T1", "T2", "T3", "T4", "T5", "US")


def main():
    sides = {(r["mid"], r["team"]): r for r in load_jsonl(OUT / "sides.jsonl")}
    mids = {m for m, t in sides}
    D = [d for d in load_jsonl(OUT / "deaths.jsonl") if d["mid"] in mids]
    E = [e for e in load_jsonl(OUT / "episodes.jsonl") if e["mid"] in mids]
    out = {"coverage": {"deaths": len(D), "episodes": len(E), "games": len({d["mid"] for d in D})}}
    for d in D:
        v = sides.get((d["mid"], d["victim_team"]))
        k = sides.get((d["mid"], "teamB" if d["victim_team"] == "teamA" else "teamA"))
        d["vt"] = v["tier"] if v else "T?"
        d["kt"] = k["tier"] if k else "T?"
        d["vwin"] = v["res"] == "win" if v else None
    print(f"champion deaths {len(D)} in {out['coverage']['games']} tracked games")
    for lab, key in (("victim", "vt"), ("killer", "kt")):
        print(f"\ndeath class % by {lab} tier (n deaths)")
        print(f"  {'tier':4s} " + " ".join(f"{c:>11s}" for c in CLS) + "   contact hp0 attackers allies6 bush")
        out[f"by_{lab}"] = {}
        for t in TIERS:
            v = [d for d in D if d[key] == t]
            if not v:
                continue
            c = collections.Counter(d["cls"] for d in v)
            row = {k: c[k] / len(v) for k in CLS}
            row.update(n=len(v), contact=median([d["contact"] for d in v]), hp0=median([d["hp0"] for d in v]),
                       attackers=mean([d["attackers"] for d in v]), allies6=mean([d["allies6"] for d in v]),
                       bush=sum(1 for d in v if d["from_bush"]) / len(v), altar=sum(1 for d in v if d["altar"]) / len(v))
            out[f"by_{lab}"][t] = row
            print(f"  {t:4s} " + " ".join(f"{100 * row[k]:10.0f}%" for k in CLS) +
                  f"   r{row['contact']} {row['hp0']:.2f} {row['attackers']:.1f} {row['allies6']:.1f} {100 * row['bush']:.0f}%  (n {len(v)})")
    # how T1 teams kill each tier, and how each tier kills T1
    print("\nT1 killers: class % by victim tier")
    out["t1_kills"] = {}
    for t in TIERS:
        v = [d for d in D if d["kt"] == "T1" and d["vt"] == t]
        if len(v) < 20:
            continue
        c = collections.Counter(d["cls"] for d in v)
        out["t1_kills"][t] = {k: c[k] / len(v) for k in CLS} | {"n": len(v)}
        print(f"  victims {t:3s} n {len(v):5d} " + ", ".join(f"{k} {100 * c[k] / len(v):.0f}" for k in CLS))
    # early deaths (r<=400) separately
    print("\ndeaths by r400: class % by victim tier")
    out["early"] = {}
    for t in TIERS:
        v = [d for d in D if d["vt"] == t and d["round"] <= 400]
        if len(v) < 20:
            continue
        c = collections.Counter(d["cls"] for d in v)
        out["early"][t] = {k: c[k] / len(v) for k in CLS} | {"n": len(v)}
        print(f"  {t:3s} n {len(v):5d} " + ", ".join(f"{k} {100 * c[k] / len(v):.0f}" for k in CLS))
    # contact episodes
    for e in E:
        s = sides.get((e["mid"], e["team"]))
        e["tier"] = s["tier"] if s else "T?"
        e["dur"] = e["r1"] - e["r0"] + 1
        e["fight"] = (e["dmg_done"] + e["dmg_taken"]) > 0
    print("\ncontact episodes (a champion with an enemy within 6, closing to 3): per tier of the champion")
    print("  tier  episodes/champ-game  fights%  P(death|fight)  P(death|quiet)  ends of fights: separation/recall/death/game_end  "
          "median fight length")
    out["episodes"] = {}
    for t in TIERS:
        v = [e for e in E if e["tier"] == t]
        if not v:
            continue
        games = len({(e["mid"], e["team"]) for e in v})
        nch = sum(len(sides[(m, tm)]["champs"]) for (m, tm) in {(e["mid"], e["team"]) for e in v})
        f = [e for e in v if e["fight"]]
        q = [e for e in v if not e["fight"]]
        ends = collections.Counter(e["end"] for e in f)
        pdf = ends["death"] / len(f) if f else None
        pdq = sum(1 for e in q if e["end"] == "death") / len(q) if q else None
        out["episodes"][t] = {"per_champ_game": len(v) / nch, "fight_share": len(f) / len(v), "p_death_fight": pdf,
                              "p_death_quiet": pdq, "ends": {k: ends[k] / len(f) for k in ("separation", "recall", "death", "game_end")},
                              "fight_len": median([e["dur"] for e in f]), "sides": games}
        print(f"  {t:4s} {len(v) / nch:6.1f} {100 * len(f) / len(v):8.0f}% {100 * pdf:13.1f}% {100 * pdq:14.1f}%   "
              + "/".join(f"{100 * ends[k] / len(f):.0f}" for k in ("separation", "recall", "death", "game_end"))
              + f"   r{median([e['dur'] for e in f])}")
    # the same by (own tier, opponent tier): the matchup decides most of it
    print("\nP(death | fight episode) and separation share by own tier x opponent tier (n fights)")
    out["episodes_matchup"] = {}
    for e in E:
        o = sides.get((e["mid"], "teamB" if e["team"] == "teamA" else "teamA"))
        e["otier"] = o["tier"] if o else "T?"
    for t in TIERS:
        line = []
        for ot in TIERS:
            f = [e for e in E if e["fight"] and e["tier"] == t and e["otier"] == ot]
            if len(f) < 100:
                continue
            pd = sum(1 for e in f if e["end"] == "death") / len(f)
            ps = sum(1 for e in f if e["end"] == "separation") / len(f)
            out["episodes_matchup"][f"{t}v{ot}"] = {"n": len(f), "p_death": pd, "p_sep": ps}
            line.append(f"vs {ot}: {100 * pd:.0f}%/{100 * ps:.0f}% ({len(f)})")
        if line:
            print(f"  {t:3s} " + "  ".join(line))
    # death risk by fight length, all tiers
    print("\nP(death) of a fight episode by its length (all tiers): ")
    bins = collections.defaultdict(lambda: [0, 0])
    for e in E:
        if not e["fight"]:
            continue
        b = 5 if e["dur"] <= 5 else 10 if e["dur"] <= 10 else 20 if e["dur"] <= 20 else 40 if e["dur"] <= 40 else 99
        bins[b][0] += e["end"] == "death"
        bins[b][1] += 1
    out["death_by_len"] = {b: bins[b][0] / bins[b][1] for b in sorted(bins)}
    print("  " + "  ".join(f"<= {b}: {100 * bins[b][0] / bins[b][1]:.0f}% (n {bins[b][1]})" for b in sorted(bins)))
    # damage ratio decides? P(death) by damage taken/done within the episode
    print("\nP(death) of a fight episode by the champion's damage taken / dealt in it (all tiers)")
    rb = collections.defaultdict(lambda: [0, 0])
    for e in E:
        if not e["fight"]:
            continue
        ratio = (e["dmg_taken"] + 1) / (e["dmg_done"] + 1)
        b = "<0.5" if ratio < 0.5 else "0.5-1" if ratio < 1 else "1-2" if ratio < 2 else "2-4" if ratio < 4 else ">=4"
        rb[b][0] += e["end"] == "death"
        rb[b][1] += 1
    out["death_by_ratio"] = {b: rb[b][0] / rb[b][1] for b in rb}
    print("  " + "  ".join(f"{b}: {100 * rb[b][0] / rb[b][1]:.0f}% (n {rb[b][1]})" for b in ("<0.5", "0.5-1", "1-2", "2-4", ">=4")))
    DATA.mkdir(parents=True, exist_ok=True)
    with open(DATA / "deaths.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()

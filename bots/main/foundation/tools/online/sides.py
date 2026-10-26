"""One record per team-side from every local match summary (no server access).

usage: python -I -X utf8 sides.py
Writes .cache/contenders/online/sides.jsonl (two lines per game) and prints coverage.
Sources: results/online/<version>/<group>/*-summary.json(.gz), results/online/ranked/*/summary.json,
.cache/main/online/summaries/*.json; metadata from our group registry and every match list held.
"""
import collections
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import (CHECK, METRICS, OUT, US, at, final, first_round_above, geometry, group_cat, jload,
                    ladder, map_info, match_meta, save_jsonl, short, summary_paths, tier_of)

LAD = ladder()
MI = map_info()


def rect_dist(x, y, rc):
    dx = max(rc["minX"] - x, 0, x - rc["maxX"])
    dy = max(rc["minY"] - y, 0, y - rc["maxY"])
    return max(dx, dy)


def side_records(mid, s, meta):
    geo = geometry()["bysha"].get(s["match"]["mapHash"])
    groups = {g["nativeId"]: g for g in geo["groups"]} if geo else {}
    spawns = geo["spawns"] if geo else None
    champs = {c["id"]: c for c in s["roster"]["champions"]}
    cmet = collections.defaultdict(dict)
    for t in s["championTimelines"]:
        cmet[t["owner"]["id"]][t["metric"]] = t["runs"]
    ev = collections.defaultdict(list)
    for e in s["events"]:
        for en in e["entries"]:
            ev[e["kind"]].append((e["owner"], en))
    rounds = s["match"]["roundsCompleted"]
    finals = {t["team"]: int(t["submittedGems"]) for t in s["teams"]}
    draft_order = [(short(d["champion"]), d["team"]) for d in s["roster"].get("draft", [])]
    mapname = geo["id"] if geo else (meta or {}).get("map", "?")
    out = []
    for team in ("teamA", "teamB"):
        other = "teamB" if team == "teamA" else "teamA"
        side = "a" if team == "teamA" else "b"
        rec = {"mid": mid, "map": mapname, "team": team, "side": side, "rounds": rounds,
               "outcome_team": s["match"]["outcome"], "engine": s["match"].get("engineVersion"),
               "gems_final": finals.get(team, 0), "ogems_final": finals.get(other, 0)}
        if mapname in MI:
            rec["mcls"] = MI[mapname]["cls"]
            rec["spawn_d"] = MI[mapname]["spawn_d"]
            rec["area"] = MI[mapname]["area"]
            rec["ndraft"] = MI[mapname]["draft"]
        if meta:
            me = [t for t in meta["teams"] if t["side"] == side][0]
            op = [t for t in meta["teams"] if t["side"] != side][0]
            lt, lo = LAD.get(me["teamId"], {}), LAD.get(op["teamId"], {})
            rec.update(name=me["name"], tid=me["teamId"], rank=lt.get("rank"), rating=lt.get("rating"),
                       tier=tier_of(lt.get("rank")), opp=op["name"], otid=op["teamId"], orank=lo.get("rank"),
                       orating=lo.get("rating"), otier=tier_of(lo.get("rank")), res=me["result"],
                       end=meta.get("endReason"), kind=meta.get("kind"), created=meta.get("createdAt"),
                       version=meta.get("version"), group=meta.get("group"), survey=meta.get("survey", False))
            if me["teamId"] == US:
                rec["tier"] = "US"
            if op["teamId"] == US:
                rec["otier"] = "US"
            # survey opponents: rank when the group was created (the snapshot may be older/newer)
            if meta.get("opp_rank_created") and op["teamId"] == US and me["teamId"] != US:
                rec["rank_created"] = meta["opp_rank_created"]
        else:
            rec.update(name="?", tid=None, rank=None, tier="T?", res="win" if s["match"]["outcome"] == team else "loss")
        ids = sorted([c["id"] for c in champs.values() if c["team"] == team], key=lambda i: champs[i]["draftPick"])
        rec["draft"] = [short(champs[i]["name"]) for i in ids]
        rec["draft_order"] = draft_order
        for metric in METRICS:
            rec[metric] = {str(r): round(sum(at(cmet[i].get(metric, []), r) for i in ids), 1) for r in CHECK}
            rec[metric]["end"] = round(sum(final(cmet[i].get(metric, [])) for i in ids), 1)
        # first champion damage dealt / taken by the team
        fd = [first_round_above(cmet[i].get("championDamageDone", [])) for i in ids]
        fd = [x for x in fd if x is not None]
        rec["first_cdmg"] = min(fd) if fd else None
        # per champion
        per = []
        death_rounds = collections.defaultdict(list)
        for o, en in ev["death"]:
            if o.get("team") == team:
                death_rounds[o.get("id")].append(en["timestamp"]["round"])
        buys = collections.defaultdict(list)
        for o, en in ev["buyItem"]:
            if o.get("team") == team:
                buys[o["id"]].append((en["timestamp"]["round"], en["target"]["name"], int(en["value"]),
                                      float(en.get("amount") or 0), en["x"], en["y"]))
        embl = collections.defaultdict(list)
        for o, en in ev["itemCombination"]:
            if o.get("team") == team:
                embl[o["id"]].append((en["timestamp"]["round"], en["target"]["name"]))
        cons = collections.defaultdict(list)
        for o, en in ev["buyConsumable"]:
            if o.get("team") == team:
                cons[o["id"]].append((en["timestamp"]["round"], en["target"]["name"], int(en["value"])))
        sells = collections.Counter(o["id"] for o, en in ev["sellItem"] if o.get("team") == team)
        for i in ids:
            c = cmet[i]
            gm = c.get("gems", [])
            maxcarry = max([float(x["value"]) for x in gm], default=0.0)
            per.append({
                "id": i, "name": short(champs[i]["name"]),
                "earned": {str(r): at(c.get("earnedGold", []), r) for r in (100, 200, 300, 400, 600, 1000)},
                "earned_end": final(c.get("earnedGold", [])),
                "spent_end": final(c.get("spentGold", [])),
                "kills": final(c.get("kills", [])), "deaths": final(c.get("deaths", [])),
                "kills400": at(c.get("kills", []), 400), "deaths400": at(c.get("deaths", []), 400),
                "cdmg": final(c.get("championDamageDone", [])), "cdmg_taken": final(c.get("championDamageTaken", [])),
                "mdmg": final(c.get("monsterDamageDone", [])), "heal": final(c.get("healingDone", [])),
                "shield": final(c.get("shieldingDone", [])), "collected": final(c.get("collectedGems", [])),
                "submitted": final(c.get("submittedGems", [])), "maxcarry": maxcarry,
                "carry300": at(gm, 300), "carry400": at(gm, 400),
                "buys": sorted(buys.get(i, [])), "emblems": sorted(embl.get(i, [])),
                "cons": sorted(cons.get(i, [])), "sells": sells.get(i, 0),
                "death_rounds": sorted(death_rounds.get(i, [])),
            })
        rec["champs"] = per
        # wards
        w = [en for o, en in ev["wardPlacement"] if o.get("team") == team]
        rec["wards400"] = sum(1 for en in w if en["timestamp"]["round"] <= 400)
        rec["wards"] = len(w)
        # clears and captures (gemCollection with campGroup context, one per recipient)
        clears = []
        for o, en in ev["campClear"]:
            if o.get("team") == team:
                g = groups.get(en["target"]["id"])
                clears.append((en["timestamp"]["round"], group_cat(g) if g else "?", en["target"]["id"]))
        rec["clears"] = sorted(clears)
        caps = collections.defaultdict(lambda: [0, set()])
        steal = 0
        for o, en in ev["gemCollection"]:
            if o.get("team") != team:
                continue
            if en["context"] == "campGroup":
                k = (en["target"]["id"], en["timestamp"]["round"])
                caps[k][0] += int(en["value"])
                caps[k][1].add(o["id"])
            elif en["context"] == "champion":
                steal += int(en["value"])
        capl = []
        for (gid, r), v in caps.items():
            g = groups.get(gid)
            capl.append((r, group_cat(g) if g else "?", gid, v[0], len(v[1]),
                         (g["gemValue"] if g else None), (g["goldValue"] if g else None)))
        rec["caps"] = sorted(capl)
        rec["stolen_gems"] = steal
        # tributes
        rec["tributes"] = sorted((en["timestamp"]["round"], int(en["value"]), o["id"], en["x"], en["y"])
                                 for o, en in ev["gemSubmission"] if o.get("team") == team)
        # deaths and kills
        dd = []
        for o, en in ev["death"]:
            if o.get("team") != team:
                continue
            tgt = en.get("target") if isinstance(en.get("target"), dict) else {}
            dd.append({"r": en["timestamp"]["round"], "ctx": en["context"], "x": en["x"], "y": en["y"],
                       "victim": short(o["name"]), "vid": o.get("id"), "killer": short(tgt.get("name", "")),
                       "kid": tgt.get("id"), "own_spawn_d": rect_dist(en["x"], en["y"], spawns[team]) if spawns else None,
                       "opp_spawn_d": rect_dist(en["x"], en["y"], spawns[other]) if spawns else None})
        rec["deaths_list"] = sorted(dd, key=lambda d: d["r"])
        kk = []
        for o, en in ev["kill"]:
            if o.get("team") != team:
                continue
            tgt = en.get("target") if isinstance(en.get("target"), dict) else {}
            kk.append({"r": en["timestamp"]["round"], "x": en["x"], "y": en["y"], "killer": short(o["name"]),
                       "victim": short(tgt.get("name", ""))})
        rec["kills_list"] = sorted(kk, key=lambda k: k["r"])
        rec["assists"] = sum(1 for o, en in ev["assist"] if o.get("team") == team)
        lost = collections.Counter()
        for o, en in ev["gemLoss"]:
            if o.get("team") == team:
                lost[en["context"]] += int(en["value"] or 0)
        rec["gems_lost_champ"] = lost["champion"]
        rec["gems_lost_camp"] = lost["camp"]
        rec["fuel_ex"] = sum(1 for o, en in ev["fuelExhausted"] if o.get("team") == team)
        rec["faults"] = s["diagnostics"]["botFaults"].get(team, 0)
        rec["resigned"] = sum(1 for o, en in ev["resignation"] if (o or {}).get("team") == team)
        out.append(rec)
    return out


def main():
    meta = match_meta()
    paths = summary_paths()
    rows, bad = [], 0
    for mid, p in sorted(paths.items()):
        try:
            s = jload(p)
            rows += side_records(mid, s, meta.get(mid))
        except Exception as e:  # a truncated or foreign file must not stop the index
            bad += 1
            print("skip", mid, type(e).__name__, str(e)[:80])
    # games are deterministic per (bots, map, side): a repeated group replays the same game. Keep the
    # earliest copy of each identical game (same map, teams, rounds, final gems and earned gold).
    byg = collections.defaultdict(list)
    for r in rows:
        byg[r["mid"]].append(r)
    seen, keep, dups = set(), [], 0
    for mid, v in sorted(byg.items(), key=lambda kv: kv[1][0].get("created") or ""):
        if len(v) == 2:
            a, b = v
            sig = (a["map"], a.get("tid"), b.get("tid"), a["rounds"], a["gems_final"], b["gems_final"],
                   a["earnedGold"]["end"], b["earnedGold"]["end"])
            if sig in seen:
                dups += 1
                continue
            seen.add(sig)
        keep += v
    print(f"dropped {dups} duplicate games (identical replays of a repeated group)")
    rows = keep
    OUT.mkdir(parents=True, exist_ok=True)
    save_jsonl(OUT / "sides.jsonl", rows)
    games = len(rows) // 2
    nometa = sum(1 for r in rows if r.get("tid") is None) // 2
    eng = collections.Counter(r.get("engine") for r in rows[::2])
    print(f"games {games} (no metadata {nometa}, unreadable {bad}); engines {dict(eng)}")
    tiers = collections.Counter(r["tier"] for r in rows)
    print("team-sides by tier", dict(sorted(tiers.items())))
    src = collections.Counter(("ours" if (r.get("tid") == US or r.get("otid") == US) else "others") for r in rows[::2])
    print("games ours/others", dict(src))


if __name__ == "__main__":
    main()

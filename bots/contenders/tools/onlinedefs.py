"""The online evidence's per-game definitions, copied so local games are measured identically.

Copied from bots/contenders/foundation/tools/online/ (22 Oct, engine 1.0.2):
  common.py   at, final, first_round_above, short, mean, median, quant, geometry, group_cat,
              spawn_center, map_info, map_class
  sides.py    side_records: one record per team-side from a match statistics dict
  trackfeat.py  track_features: cohesion, spread, recalls, contact place, party size
  deathctx.py   deaths_and_episodes: death classes (O14) and contact episodes (O13)
The only adaptations: the functions take in-memory data (a statistics dict, whose JSON schema is
the online summary's, and decoded tracks in the foundation's compact form) instead of paths, and
side_records has no online match metadata (no ranks or names). Every formula, threshold and loop is
unchanged; tests/test_tools.py checks that these copies reproduce the foundation's cached outputs
line for line on online games. Change a definition only together with the foundation, or local
numbers stop being comparable with online.md.
"""
import collections
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

# ----------------------------------------------------------------------------- common.py
NAME = {"Vesper Vale": "Vesper", "Cassi & Issac": "C&I", "Heart of Ruin": "Heart"}
CHECK = (25, 50, 75, 100, 150, 200, 250, 300, 400, 500, 600, 800, 1000, 1200, 1500, 2000)
METRICS = ("earnedGold", "spentGold", "gems", "collectedGems", "submittedGems", "kills", "deaths",
           "monsterDamageDone", "monsterDamageTaken", "championDamageDone", "championDamageTaken",
           "healingDone", "shieldingDone")
TYPES = ["Ryth", "Vesper", "Vendetta", "Cassi", "Issac", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg",
         "Tavi", "Heart"]
RANGED = {"Vesper", "Stellar", "Cassi", "Willow", "Tavi", "Prism"}

_geo = None


def geometry():
    global _geo
    if _geo is None:
        with open(ROOT / "maps" / "geometry.json", encoding="utf-8") as f:
            g = json.load(f)
        _geo = {"bysha": {m["mapSha256"]: m for m in g["maps"]}, "byid": {m["id"]: m for m in g["maps"]}}
    return _geo


def group_cat(gr):
    t = collections.Counter(m["type"] for m in gr["members"])
    for big in ("monolith", "golem", "elk", "wolf"):
        if t.get(big):
            return big
    if t.get("gold_cluster") and t.get("gem_cluster"):
        return "mix"
    return "gold" if t.get("gold_cluster") else "gem"


def spawn_center(rc):
    return ((rc["minX"] + rc["maxX"]) / 2.0, (rc["minY"] + rc["maxY"]) / 2.0)


_mi = None


def map_info():
    """Per pool map: size, draft count, spawn distance (Chebyshev between spawn centres), gem and gold
    rates (sum value/cooldown), jackpot share (largest group's share of the gem rate) and a class."""
    global _mi
    if _mi is not None:
        return _mi
    out = {}
    for mid, m in geometry()["byid"].items():
        a, b = spawn_center(m["spawns"]["teamA"]), spawn_center(m["spawns"]["teamB"])
        sd = max(abs(a[0] - b[0]), abs(a[1] - b[1]))
        rates = [gr["gemValue"] / gr["cooldown"] for gr in m["groups"] if gr.get("cooldown")]
        gem = sum(rates)
        gold = sum(gr["goldValue"] / gr["cooldown"] for gr in m["groups"] if gr.get("cooldown"))
        jack = max(rates) / gem if gem else 0.0
        harmless = sum(gr["gemValue"] / gr["cooldown"] for gr in m["groups"] if gr.get("cooldown")
                       and group_cat(gr) in ("gem", "gold", "mix"))
        out[mid] = {"w": m["width"], "h": m["height"], "area": m["width"] * m["height"], "draft": m["draftCount"],
                    "spawn_d": sd, "gem_rate": gem, "gold_rate": gold, "jackpot": jack, "harmless_gem_rate": harmless}
    for mid, v in out.items():
        v["cls"] = map_class(v)
    _mi = out
    return out


def map_class(v):
    """Coarse map classes for online.md (independent of the 00/01 name prefix, which is not size)."""
    if v["spawn_d"] <= 15:
        return "touch"          # spawns within 15 tiles: contact from round 1
    if v["harmless_gem_rate"] >= 4.0 or v["gem_rate"] >= 12.0:
        return "gemrich"
    if v["spawn_d"] >= 60:
        return "far"
    return "mid"


def at(runs, r):
    """Value of a run-length timeline at round r (value of the run covering r)."""
    t, v = 0, 0.0
    for x in runs:
        if t > r:
            break
        v = float(x["value"])
        t += x["duration"]
    return v


def final(runs):
    return float(runs[-1]["value"]) if runs else 0.0


def first_round_above(runs, thr=0.0):
    t = 0
    for x in runs:
        if float(x["value"]) > thr:
            return t
        t += x["duration"]
    return None


def short(name):
    return NAME.get(name, name)


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else None


def median(xs):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    n = len(xs)
    return xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2.0


def quant(xs, q):
    xs = sorted(x for x in xs if x is not None)
    if not xs:
        return None
    i = min(len(xs) - 1, max(0, int(round(q * (len(xs) - 1)))))
    return xs[i]


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def rect_dist(p, rc):
    dx = max(rc["minX"] - p[0], 0, p[0] - rc["maxX"])
    dy = max(rc["minY"] - p[1], 0, p[1] - rc["maxY"])
    return max(dx, dy)


# ----------------------------------------------------------------------------- sides.py
def _rect_dist_xy(x, y, rc):
    dx = max(rc["minX"] - x, 0, x - rc["maxX"])
    dy = max(rc["minY"] - y, 0, y - rc["maxY"])
    return max(dx, dy)


def side_records(mid, s):
    """sides.side_records without online metadata: name "?", tier "T?", res from the outcome
    (a draw is "draw"; the online copy, which always has metadata, never reaches this branch)."""
    MI = map_info()
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
    mapname = geo["id"] if geo else "?"
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
        outcome = s["match"]["outcome"]
        rec.update(name="?", tid=None, rank=None, tier="T?",
                   res="draw" if outcome == "draw" else "win" if outcome == team else "loss")
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
                       "kid": tgt.get("id"), "own_spawn_d": _rect_dist_xy(en["x"], en["y"], spawns[team]) if spawns else None,
                       "opp_spawn_d": _rect_dist_xy(en["x"], en["y"], spawns[other]) if spawns else None})
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


# ----------------------------------------------------------------------------- trackfeat.py
def per_round(s, metric):
    """champion id -> {round: increase of `metric` that round} from the run-length timelines
    (deathctx.per_round; trackfeat.per_round_damage is this with championDamageDone)."""
    out = {}
    for t in s["championTimelines"]:
        if t["metric"] != metric:
            continue
        d, r, prev = {}, 0, 0.0
        for x in t["runs"]:
            v = float(x["value"])
            if v > prev:
                d[r] = v - prev
            prev = v
            r += x["duration"]
        out[t["owner"]["id"]] = d
    return out


def track_features(mid, tr, s):
    """trackfeat.feats on decoded tracks `tr` and statistics `s`: one dict per team (teamA, teamB)."""
    geo = geometry()["bysha"].get(s["match"]["mapHash"])
    if not geo:
        return []
    spawns = {0: geo["spawns"]["teamA"], 1: geo["spawns"]["teamB"]}
    cen = {k: spawn_center(v) for k, v in spawns.items()}
    pos = collections.defaultdict(dict)   # round -> id -> (x, y, hp)
    team_of, name_of = {}, {}
    for t in tr["tracks"]:
        team_of[t["id"]] = t["team"]
        name_of[t["id"]] = TYPES[t["type"]] if 0 <= t["type"] < len(TYPES) else str(t["type"])
        for r, x, y, hp, gold, gems in t["s"]:
            if x >= 0 and hp > 0:
                pos[r][t["id"]] = (x, y, hp)
    dmg = per_round(s, "championDamageDone")
    rounds = sorted(pos)
    res = []
    for team in (0, 1):
        other = 1 - team
        mine = [i for i in team_of if team_of[i] == team]
        f = {"mid": mid, "team": "teamA" if team == 0 else "teamB"}
        coh = {300: collections.Counter(), 1000: collections.Counter()}
        spread, near400, enemyhalf, spawnr, alive1000 = [], 0, 0, 0, 0
        eh300, alive300 = 0, 0
        recalls = collections.Counter()
        prevpos = {}
        for r in rounds:
            if r > 1000:
                break
            P = pos[r]
            al = [(i, P[i]) for i in mine if i in P]
            en = [P[i] for i in P if team_of.get(i) == other]
            for i, a in al:
                k = sum(1 for j, b in al if j != i and cheb(a, b) <= 4)
                coh[300 if r <= 300 else 1000][min(k, 2)] += 1
                alive1000 += 1
                if r <= 400 and any(cheb(a, e) <= 6 for e in en):
                    near400 += 1
                d_own = cheb(a, cen[team])
                d_opp = cheb(a, cen[other])
                if d_opp < d_own:
                    enemyhalf += 1
                    if r <= 300:
                        eh300 += 1
                if r <= 300:
                    alive300 += 1
                if rect_dist(a, spawns[team]) <= 1:
                    spawnr += 1
                pp = prevpos.get(i)
                if pp and rect_dist(a, spawns[team]) == 0 and rect_dist(pp, spawns[team]) >= 5 and r - pp[2] <= 1:
                    recalls[400 if r <= 400 else 1000] += 1
                prevpos[i] = (a[0], a[1], r)
            if r <= 300 and len(al) >= 2:
                ds = [cheb(a, b) for x, (i, a) in enumerate(al) for (j, b) in al[x + 1:]]
                spread.append(sum(ds) / len(ds))
        for c in (300, 1000):
            n = sum(coh[c].values())
            f[f"coh{c}"] = [coh[c][k] / n for k in range(3)] if n else None
        f["spread300"] = sum(spread) / len(spread) if spread else None
        f["near400"] = near400
        f["enemyhalf"] = enemyhalf / alive1000 if alive1000 else None
        f["enemyhalf300"] = eh300 / alive300 if alive300 else None
        f["spawn"] = spawnr / alive1000 if alive1000 else None
        nch = max(1, len(mine))
        f["recalls400"] = recalls[400] / nch
        f["recalls1000"] = (recalls[400] + recalls[1000]) / nch
        # first champion damage of the game: place of the closest enemy pair that round
        first = None
        for i, d in dmg.items():
            if d:
                r0 = min(d)
                if first is None or r0 < first:
                    first = r0
        f["contact_round"] = first
        if first is not None and first in pos:
            P = pos[first]
            al = [P[i] for i in mine if i in P]
            en = [P[i] for i in P if team_of.get(i) == other]
            best = None
            for a in al:
                for e in en:
                    d = cheb(a, e)
                    if best is None or d < best[0]:
                        best = (d, ((a[0] + e[0]) / 2.0, (a[1] + e[1]) / 2.0))
            if best:
                m = best[1]
                do, de = cheb(m, cen[team]), cheb(m, cen[other])
                f["contact_frac"] = do / (do + de) if do + de else 0.5
                f["contact_gap"] = best[0]
        # damage rounds: party size and ranged hold-back
        party, held, dists = [], 0, []
        nr = 0
        for i in mine:
            for r, v in dmg.get(i, {}).items():
                if r > 1000 or r not in pos or i not in pos[r]:
                    continue
                P = pos[r]
                a = P[i]
                party.append(1 + sum(1 for j in mine if j != i and j in P and cheb(a, P[j]) <= 4))
                if name_of.get(i) in RANGED:
                    en = [P[j] for j in P if team_of.get(j) == other]
                    if en:
                        dmin = min(cheb(a, e) for e in en)
                        dists.append(dmin)
                        nr += 1
                        held += dmin >= 2
        f["party"] = sum(party) / len(party) if party else None
        f["ranged_dmg_rounds"] = nr
        f["ranged_hold"] = held / nr if nr else None
        f["ranged_dist_med"] = sorted(dists)[len(dists) // 2] if dists else None
        res.append(f)
    return res


# ----------------------------------------------------------------------------- deathctx.py
def deaths_and_episodes(mid, tr, s):
    """deathctx.analyse on decoded tracks `tr` and statistics `s`: (deaths, episodes).
    Death record: contact length (rounds with an enemy within 6 before the death, a gap of 3 rounds
    ends a contact), victim health share at contact start, enemies within 3 in the last 3 rounds,
    allies within 6 three rounds before death, distance walked toward own spawn during the contact,
    share of contact rounds with an enemy within 2, monster damage taken in the last 10 rounds, whether
    a killer-team champion stood in a bush within 6 of the victim at contact start, death place.
    Primary class, first match: camp, home, burst, chase, picked, outnumbered, duel, brawl.
    Episode (per champion): consecutive rounds with an enemy within 6 that include one within 3;
    ends by death, recall (arrival on own spawn from >= 5 tiles) or separation (no enemy within 6 for
    3 rounds); damage exchanged by that champion during it; duration."""
    geo = geometry()["bysha"].get(s["match"]["mapHash"])
    if not geo:
        return [], []
    W = geo["width"]
    bush = set(geo["terrain"].get("bush", []))
    altars = [(t % W, t // W) for t in geo["terrain"].get("altar", [])]
    spawns = {0: geo["spawns"]["teamA"], 1: geo["spawns"]["teamB"]}
    cen = {k: spawn_center(v) for k, v in spawns.items()}
    pos = collections.defaultdict(dict)
    team_of, name_of = {}, {}
    for t in tr["tracks"]:
        team_of[t["id"]] = t["team"]
        name_of[t["id"]] = TYPES[t["type"]] if 0 <= t["type"] < len(TYPES) else str(t["type"])
        for r, x, y, hp, gold, gems in t["s"]:
            if x >= 0 and hp > 0:
                pos[r][t["id"]] = (x, y, hp)
    mdmg_taken = per_round(s, "monsterDamageTaken")
    cdone = per_round(s, "championDamageDone")
    ctaken = per_round(s, "championDamageTaken")
    last = s["match"]["roundsCompleted"]
    deaths = []
    for e in s["events"]:
        if e["kind"] != "death":
            continue
        for en in e["entries"]:
            if en["context"] != "champion":
                continue
            vid = e["owner"].get("id")
            if vid not in team_of:
                continue
            rd = en["timestamp"]["round"]
            vt = team_of[vid]
            kt = 1 - vt
            # contact window: walk back while an enemy is within 6 (gaps of up to 2 rounds allowed)
            start, gap, r = rd, 0, rd - 1
            while r >= max(1, rd - 80):
                P = pos.get(r, {})
                a = P.get(vid)
                if a is None:
                    break
                near = any(cheb(a, P[j]) <= 6 for j in P if team_of.get(j) == kt)
                if near:
                    start, gap = r, 0
                else:
                    gap += 1
                    if gap >= 3:
                        break
                r -= 1
            clen = rd - start
            hp_hist = [pos[r][vid][2] for r in range(max(1, rd - 120), rd) if vid in pos.get(r, {})]
            mx = max(hp_hist) if hp_hist else None
            a0 = pos.get(start, {}).get(vid)
            hp0 = (a0[2] / mx) if (a0 and mx) else None
            att = set()
            for r in range(rd - 3, rd + 1):
                P = pos.get(r, {})
                a = P.get(vid)
                if a:
                    att |= {j for j in P if team_of.get(j) == kt and cheb(a, P[j]) <= 3}
            P3 = pos.get(rd - 3, {})
            a3 = P3.get(vid)
            allies6 = sum(1 for j in P3 if team_of.get(j) == vt and j != vid and a3 and cheb(a3, P3[j]) <= 6)
            enem6 = sum(1 for j in P3 if team_of.get(j) == kt and a3 and cheb(a3, P3[j]) <= 6)
            close = 0
            for r in range(start, rd):
                P = pos.get(r, {})
                a = P.get(vid)
                if a and any(cheb(a, P[j]) <= 2 for j in P if team_of.get(j) == kt):
                    close += 1
            alast = None
            for r in range(rd, start - 1, -1):
                if vid in pos.get(r, {}):
                    alast = pos[r][vid]
                    break
            walked_home = (cheb(a0, cen[vt]) - cheb(alast, cen[vt])) if (a0 and alast) else None
            md = sum(v for r, v in mdmg_taken.get(vid, {}).items() if rd - 10 <= r <= rd)
            from_bush = False
            if a0:
                P0 = pos.get(start, {})
                from_bush = any(team_of.get(j) == kt and cheb(a0, P0[j]) <= 6 and (P0[j][1] * W + P0[j][0]) in bush for j in P0)
            dp = (en["x"], en["y"])
            home = rect_dist(dp, spawns[vt]) <= 6
            dived = rect_dist(dp, spawns[kt]) <= 6
            altar = any(cheb(dp, al) <= 2 for al in altars)
            if mx and md >= 0.25 * mx:
                cls = "camp"
            elif home:
                cls = "home"
            elif clen <= 5 and (hp0 or 0) >= 0.7:
                cls = "burst"
            elif clen > 5 and (walked_home or 0) >= 3 and close >= 0.5 * max(1, clen):
                cls = "chase"
            elif allies6 == 0 and enem6 >= 2:
                cls = "picked"
            elif enem6 > allies6 + 1:
                cls = "outnumbered"
            elif allies6 == 0 and enem6 <= 1:
                cls = "duel"
            else:
                cls = "brawl"
            deaths.append({"mid": mid, "round": rd, "victim_team": "teamA" if vt == 0 else "teamB", "victim": name_of[vid],
                           "killer": short((en.get("target") or {}).get("name", "")) if isinstance(en.get("target"), dict) else "",
                           "contact": clen, "hp0": hp0, "attackers": len(att), "allies6": allies6, "enemies6": enem6,
                           "walked_home": walked_home, "close_share": close / max(1, clen), "monster_dmg10": md,
                           "maxhp": mx, "from_bush": from_bush, "home": home, "dived": dived, "altar": altar, "cls": cls})
    # contact episodes per champion
    eps = []
    for cid, t in team_of.items():
        other = 1 - t
        cur = None
        far = 0
        prev = None
        for r in range(1, last + 1):
            P = pos.get(r, {})
            a = P.get(cid)
            if a is None:
                if cur:
                    cur["end"] = "death" if r < last else "game_end"
                    cur["r1"] = r
                    eps.append(cur)
                    cur = None
                prev = None
                continue
            dmin = min([cheb(a, P[j]) for j in P if team_of.get(j) == other], default=99)
            if cur is None:
                if dmin <= 6:
                    cur = {"r0": r, "min": dmin, "close": 0}
                    far = 0
            if cur is not None:
                cur["min"] = min(cur["min"], dmin)
                cur["close"] += dmin <= 2
                if prev and rect_dist(a, spawns[t]) == 0 and rect_dist(prev, spawns[t]) >= 5:
                    cur["end"], cur["r1"] = "recall", r
                    eps.append(cur)
                    cur = None
                elif dmin > 6:
                    far += 1
                    if far >= 3:
                        cur["end"], cur["r1"] = "separation", r - 2
                        eps.append(cur)
                        cur = None
                else:
                    far = 0
            prev = a
        if cur:
            cur["end"], cur["r1"] = "game_end", last
            eps.append(cur)
        for e in eps:
            if "cid" in e:
                continue
            e["cid"] = cid
            e["team"] = "teamA" if t == 0 else "teamB"
            e["name"] = name_of[cid]
            e["dmg_done"] = sum(v for r, v in cdone.get(cid, {}).items() if e["r0"] <= r <= e["r1"])
            e["dmg_taken"] = sum(v for r, v in ctaken.get(cid, {}).items() if e["r0"] <= r <= e["r1"])
            e["mid"] = mid
    eps = [e for e in eps if e["min"] <= 3]
    return deaths, eps

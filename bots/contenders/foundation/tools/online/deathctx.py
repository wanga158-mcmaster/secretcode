"""How champions die and how contact ends, from replay tracks joined with summaries (all tiers).

usage: python -I -X utf8 deathctx.py [--workers 4]
Inputs: tracks/<mid>.json.gz, match summaries, maps/geometry.json (bushes, altars, spawns).
Writes .cache/contenders/online/deaths.jsonl (one line per champion death by a champion) and
.cache/contenders/online/episodes.jsonl (one line per champion contact episode), then prints tables
by the tier of the victim / of the champion.

Death record: contact length (rounds with an enemy within 6 before the death, a gap of 3 rounds
ends a contact), victim health share at contact start, enemies within 3 in the last 3 rounds,
allies within 6 three rounds before death, distance walked toward own spawn during the contact,
share of contact rounds with an enemy within 2, monster damage taken in the last 10 rounds, whether
a killer-team champion stood in a bush within 6 of the victim at contact start, death place
(own spawn <= 6, enemy spawn <= 6, altar <= 2). Primary class, first match in this order:
camp (monster damage >= 25% of max HP in the last 10 rounds), home (died within 6 of own spawn),
burst (contact <= 5 rounds from >= 70% health), chase (contact > 5, walked >= 3 toward own spawn,
an enemy within 2 in >= 50% of contact rounds), picked (no ally within 6, 2+ enemies), outnumbered
(enemies within 6 > allies within 6 + 1), duel (no ally, at most one enemy within 6), brawl (the rest).
Episode record (per champion): consecutive rounds with an enemy within 6 that include one within 3;
ends by death, recall (arrival on own spawn from >= 5 tiles) or separation (no enemy within 6 for 3
rounds); damage exchanged by that champion during it; duration.
"""
import collections
import concurrent.futures as cf
import gzip
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import OUT, geometry, jload, load_jsonl, mean, median, save_jsonl, short, spawn_center, summary_paths

TR = OUT / "tracks"
TYPES = ["Ryth", "Vesper", "Vendetta", "Cassi", "Issac", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg",
         "Tavi", "Heart"]


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def rect_dist(p, rc):
    dx = max(rc["minX"] - p[0], 0, p[0] - rc["maxX"])
    dy = max(rc["minY"] - p[1], 0, p[1] - rc["maxY"])
    return max(dx, dy)


def per_round(s, metric):
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


def analyse(mid, sp):
    p = TR / f"{mid}.json.gz"
    if not p.exists():
        return [], []
    with gzip.open(p, "rt", encoding="utf-8") as f:
        tr = json.load(f)
    s = jload(sp)
    geo = geometry()["bysha"].get(s["match"]["mapHash"])
    if not geo:
        return [], []
    W = geo["width"]
    bush = set(geo["terrain"].get("bush", []))
    altars = [(t % W, t // W) for t in geo["terrain"].get("altar", [])]
    spawns = {0: geo["spawns"]["teamA"], 1: geo["spawns"]["teamB"]}
    cen = {k: spawn_center(v) for k, v in spawns.items()}
    pos = collections.defaultdict(dict)
    team_of, name_of, hpmax = {}, {}, collections.defaultdict(float)
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


def main():
    workers = 4
    if "--workers" in sys.argv:
        workers = int(sys.argv[sys.argv.index("--workers") + 1])
    sp = summary_paths()
    mids = [p.name[:-8] for p in TR.glob("*.json.gz") if p.name[:-8] in sp]
    D, E = [], []
    with cf.ProcessPoolExecutor(max_workers=workers) as ex:
        for d, e in ex.map(analyse, mids, [sp[m] for m in mids], chunksize=8):
            D += d
            E += e
    save_jsonl(OUT / "deaths.jsonl", D)
    save_jsonl(OUT / "episodes.jsonl", E)
    print(f"{len(mids)} games: {len(D)} champion deaths, {len(E)} contact episodes")


if __name__ == "__main__":
    main()

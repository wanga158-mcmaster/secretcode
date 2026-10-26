"""Movement features per team-side from replay tracks joined with summaries.

usage: python -I -X utf8 trackfeat.py [--workers 4]
Inputs: .cache/contenders/online/tracks/<mid>.json.gz (tracks.py), the match summaries.
Writes .cache/contenders/online/trackfeat.jsonl, one line per (match, team) with:
  coh300 / coh1000: share of alive champion-rounds with 0, 1, 2+ allies within 4 (Chebyshev),
    rounds 1-300 and 301-1000; spread300: mean pairwise distance of alive allies, r1-300;
  near400: alive champion-rounds with an enemy within 6, r1-400; enemyhalf: share of alive
    champion-rounds nearer the enemy spawn than our own, r1-1000; spawn: share on/next to our spawn;
  recalls400 / recalls1000: arrivals on our spawn from >= 5 tiles away in one round, per champion;
  contact: round and place of the first champion damage (fraction of the way from our spawn to theirs,
    measured at the closest enemy pair); dmg_range: for ranged champions, share of their
    champion-damage rounds with no enemy within 1 (hold-back) and median distance;
  party: mean party size (dealer + allies within 4) on rounds the team dealt champion damage, r<=1000.
"""
import collections
import concurrent.futures as cf
import gzip
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from common import OUT, geometry, jload, save_jsonl, short, spawn_center, summary_paths

TR = OUT / "tracks"
RANGED = {"Vesper", "Stellar", "Cassi", "Willow", "Tavi", "Prism"}
TYPES = ["Ryth", "Vesper", "Vendetta", "Cassi", "Issac", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg",
         "Tavi", "Heart"]


def cheb(a, b):
    return max(abs(a[0] - b[0]), abs(a[1] - b[1]))


def rect_dist(p, rc):
    dx = max(rc["minX"] - p[0], 0, p[0] - rc["maxX"])
    dy = max(rc["minY"] - p[1], 0, p[1] - rc["maxY"])
    return max(dx, dy)


def per_round_damage(s):
    """champion id -> {round: champion damage dealt that round} from the run-length timelines."""
    out = {}
    for t in s["championTimelines"]:
        if t["metric"] != "championDamageDone":
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


def feats(mid, sp):
    p = TR / f"{mid}.json.gz"
    if not p.exists():
        return []
    with gzip.open(p, "rt", encoding="utf-8") as f:
        tr = json.load(f)
    s = jload(sp) if sp else None
    if s is None:
        return []
    geo = geometry()["bysha"].get(s["match"]["mapHash"])
    if not geo:
        return []
    spawns = {0: geo["spawns"]["teamA"], 1: geo["spawns"]["teamB"]}
    cen = {k: spawn_center(v) for k, v in spawns.items()}
    roster = {c["id"]: c for c in s["roster"]["champions"]}
    pos = collections.defaultdict(dict)   # round -> id -> (x, y, hp)
    team_of, name_of = {}, {}
    for t in tr["tracks"]:
        team_of[t["id"]] = t["team"]
        name_of[t["id"]] = TYPES[t["type"]] if 0 <= t["type"] < len(TYPES) else str(t["type"])
        for r, x, y, hp, gold, gems in t["s"]:
            if x >= 0 and hp > 0:
                pos[r][t["id"]] = (x, y, hp)
    dmg = per_round_damage(s)
    rounds = sorted(pos)
    last = s["match"]["roundsCompleted"]
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


def main():
    workers = 4
    if "--workers" in sys.argv:
        workers = int(sys.argv[sys.argv.index("--workers") + 1])
    sp = summary_paths()
    mids = [p.name[:-8] for p in TR.glob("*.json.gz") if p.name[:-8] in sp]
    rows = []
    with cf.ProcessPoolExecutor(max_workers=workers) as ex:
        for r in ex.map(feats, mids, [sp[m] for m in mids], chunksize=8):
            rows += r
    save_jsonl(OUT / "trackfeat.jsonl", rows)
    print(f"track features for {len(rows) // 2} games ({len(mids)} with tracks and summaries)")


if __name__ == "__main__":
    main()

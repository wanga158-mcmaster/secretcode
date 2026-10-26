"""Shared loaders for the contenders' online evidence (foundation/online.md, O-entries).

Read-only over local data: results/online/ (our match groups, registry, ranked), the main agent's
caches under .cache/main/ (summaries, match lists, ladder snapshots, replays). Nothing here
contacts the server. Run every script with the runtime python and -I -X utf8.
"""
import collections
import glob
import gzip
import io
import json
import os
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
OUT = ROOT / ".cache" / "contenders" / "online"          # bulky regenerable output
DATA = ROOT / "bots" / "main" / "foundation" / "data" / "online"   # small durable tables
ONLINE = ROOT / "results" / "online"
MAIN = ROOT / ".cache" / "main"
ECO = MAIN / "analysis" / "ecosystem"
US = "34b36bab-66bb-4506-b032-2df6a081758d"  # ha²t
PICKS = ["Ryth", "Vesper", "Vendetta", "C&I", "Stellar", "Prism", "Marcella", "Willow", "Ren", "Varg",
         "Tavi", "Heart"]
NAME = {"Vesper Vale": "Vesper", "Cassi & Issac": "C&I", "Heart of Ruin": "Heart"}
META5 = {"Ryth", "Vesper", "Vendetta", "Marcella", "Heart"}
CHECK = (25, 50, 75, 100, 150, 200, 250, 300, 400, 500, 600, 800, 1000, 1200, 1500, 2000)
METRICS = ("earnedGold", "spentGold", "gems", "collectedGems", "submittedGems", "kills", "deaths",
           "monsterDamageDone", "monsterDamageTaken", "championDamageDone", "championDamageTaken",
           "healingDone", "shieldingDone")


def jload(path, retries=5):
    """JSON from a file that another process may be rewriting (the keeper writes groups.json)."""
    for i in range(retries):
        try:
            p = str(path)
            if p.endswith(".gz"):
                with gzip.open(p, "rt", encoding="utf-8") as f:
                    return json.load(f)
            with open(p, encoding="utf-8") as f:
                return json.load(f)
        except (ValueError, OSError):
            if i == retries - 1:
                raise
            time.sleep(1.0)


# Win expectation from the ladder rating gap (snapshot 22 Oct 03:2x UTC). The platform's ratings are
# compressed: in the 03:10-03:20 window (1,922 games, median gap 9.6 points) the higher-rated side
# won 67%, where a 400-point Elo curve predicts 52%. Fitted on that window plus the 446 other-team
# summary sides: p = eps + (1 - 2 eps) / (1 + exp(-k gap)), k 0.097 per point, eps 0.16 (a lapse
# rate: even large gaps win only ~80-88%). Calibration: window gap 0-25 actual 0.64 / fit 0.64,
# 25-50 0.88 / 0.81; other-team games gap 0-25 0.56 / 0.65, 25-50 0.65 / 0.80 (their ratings are
# from a later snapshot than the games, so they predict less). Residuals built on it are
# rating-adjusted associations, not causal effects.
RATING_K = 0.097
RATING_EPS = 0.16


def expected(r, ro):
    import math
    return RATING_EPS + (1 - 2 * RATING_EPS) / (1.0 + math.exp(-RATING_K * (r - ro)))


def tier_of(rank):
    """Rank tiers used throughout online.md (same cut points as main's ecosystem catalog)."""
    if rank is None:
        return "T?"
    if rank <= 10:
        return "T1"
    if rank <= 50:
        return "T2"
    if rank <= 150:
        return "T3"
    if rank <= 400:
        return "T4"
    return "T5"


def ladder():
    """teamId -> {rank, name, rating, matches} from the newest full ladder snapshot (22 Oct 03:2x UTC)."""
    out = {}
    for f in sorted((ECO / "ladder").glob("p*.json")):
        for t in jload(f).get("teams", []):
            out[t["teamId"]] = {"rank": t["rank"], "name": t["name"], "rating": t["rating"],
                                "matches": t.get("matches")}
    return out


def registry():
    return jload(ONLINE / "groups.json")


def _list_files():
    files = sorted((ECO / "lists").glob("*.json")) + sorted((MAIN / "analysis" / "lists").glob("*.json"))
    files += sorted((MAIN / "analysis").glob("ml_*.json")) + [MAIN / "ml.json"]
    return [f for f in files if f.exists()]


def match_meta():
    """matchId -> list-entry dict (teams with names, ids, results, draftPicks, engine; map; endReason;
    kind; createdAt) merged from every match list we hold plus our group registry. Adds
    'version'/'group'/'survey'/'opp_rank_created' for our groups."""
    out = {}

    def add(m, extra=None):
        if not m.get("teams") or m.get("status") not in (None, "finished"):
            return
        if any(t.get("result") is None for t in m["teams"]):
            return
        cur = out.setdefault(m["matchId"], dict(m))
        if extra:
            cur.update(extra)

    for f in _list_files():
        try:
            d = jload(f)
        except Exception:
            continue
        for m in d.get("matches", []):
            add(m)
    top = MAIN / "online" / "top.json"
    if top.exists():
        for tid, v in jload(top).items():
            for m in v.get("matches", []):
                add(m)
    for g in registry():
        for m in (g.get("show") or {}).get("matches", []):
            add(m, {"version": str(g["version"]), "group": g["group"], "survey": g.get("kind") == "survey",
                    "opp_rank_created": (g.get("created") or {}).get("rank")})
    return out


def summary_paths():
    """matchId -> path of its JSON summary (first found), across all local sources."""
    out = {}
    for p in glob.glob(str(ONLINE / "*" / "*" / "*-summary.json*")):
        mid = os.path.basename(p).split("-summary.json")[0]
        out.setdefault(mid, p)
    for p in glob.glob(str(ONLINE / "ranked" / "*" / "summary.json")):
        out.setdefault(Path(p).parent.name, p)
    for p in glob.glob(str(MAIN / "online" / "summaries" / "*.json")):
        out.setdefault(Path(p).stem, p)
    return out


def replay_sources():
    """matchId -> (container zip, inner name or None). Inner match zips are what the replay inspector
    reads (the CLI replay bundle)."""
    out = {}
    for gz in glob.glob(str(ONLINE / "*" / "*" / "group.zip")):
        try:
            z = zipfile.ZipFile(gz)
        except zipfile.BadZipFile:
            continue
        for i in z.infolist():
            n = i.filename
            if n.endswith(".zip") and "-" in n:
                mid = n[3:-4] if n[2] == "-" else n[:-4]
                out.setdefault(mid, (gz, n))
    for p in glob.glob(str(ONLINE / "ranked" / "*" / "match.zip")):
        out.setdefault(Path(p).parent.name, (p, None))
    full = set(out) | set(summary_paths())
    byprefix = collections.defaultdict(list)
    for k in full:
        byprefix[k[:8]].append(k)
    for d in (MAIN / "analysis" / "dl", ECO / "dl", MAIN / "analysis" / "rp"):
        for p in glob.glob(str(d / "*.zip")):
            stem = Path(p).stem
            if len(stem) == 8:  # main's rp/ names replays by match-id prefix
                cand = byprefix.get(stem, [])
                if len(cand) != 1:
                    continue
                stem = cand[0]
            out.setdefault(stem, (p, None))
    return out


def extract_replay(src, dest):
    """Write the CLI replay bundle for one match to dest."""
    container, inner = src
    if inner is None:
        data = open(container, "rb").read()
    else:
        data = zipfile.ZipFile(container).read(inner)
    with open(dest, "wb") as f:
        f.write(data)


_geo = None


def geometry():
    global _geo
    if _geo is None:
        g = jload(ROOT / "maps" / "geometry.json")
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


def map_info():
    """Per pool map: size, draft count, spawn distance (Chebyshev between spawn centres), gem and gold
    rates (sum value/cooldown), jackpot share (largest group's share of the gem rate) and a class."""
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


def wilson(k, n, z=1.96):
    """95% Wilson interval for k successes of n, as (lo, hi)."""
    if n == 0:
        return (None, None)
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    s = z * ((p * (1 - p) + z * z / (4 * n)) / n) ** 0.5
    return ((c - s) / d, (c + s) / d)


def load_jsonl(path):
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def save_jsonl(path, rows):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")

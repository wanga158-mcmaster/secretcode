"""Fundamentals of local arena games, measured exactly as the online evidence measures them.

  python -B -I -X utf8 bots/contenders/tools/fundamentals.py RUN_DIR [RUN_DIR ...] [--by-opponent]
         [--by-map] [--side subject|opponent] [--subject SEL] [--online] [--json OUT] [--workers N]
         [--compact]
  python -B -I -X utf8 bots/contenders/tools/fundamentals.py --online-reference

RUN_DIR is any arena run, sweep or battery directory whose games kept replays and statistics
(`--keep-replays all`; a statistics file may be compacted to <name>.json.gz, as battery.py does
after decoding). For each game the replay is decoded with tools/inspect-strategy-replay.mjs
(positions at each champion's turn start, as online) and measured by onlinedefs.py, a verbatim copy
of the foundation's per-game code (foundation/tools/online: sides, trackfeat, deathctx). The
aggregates below copy the foundation's table code (extra, deathtab, items, econ, style), so every
number is defined as in foundation/online.md and compares directly with its tiers:
  gold r400 / r1000  earned gold per alive champion-round over rounds 1-400 (1-1000): team earned
           gold at R / (bodies x R - dead champion-rounds by R); median over sides that reached R.
           Earned gold includes kill gold (O2: T1 1.05, T2 0.92, T3 0.82, us 0.84).
  dead400  dead champion-rounds by r400: sum over deaths by r400 of min(60, 400 - death round);
           mean over sides (O2: T1 102, us 132).
  gems/100 collected gems per 100 alive champion-rounds, r1-400, median (O2: T1 12.6, us 9.3).
  P(d|f)   contact episode (per champion): consecutive turn starts with an enemy within 6 that
           close to <= 3; ends at death, recall (arrival on own spawn from >= 5 tiles) or separation
           (3 turn starts with no enemy within 6). A fight is an episode with champion damage dealt
           or taken by that champion. P(death | fight) and the end shares sep/rec/death are pooled
           over fights (O13: T1 13%, T2 12%, us 30%; T1 ends 75/10/13).
  classes  champion-caused deaths, first class that applies: camp, home, burst, chase, picked,
           outnumbered, duel, brawl (O14).
  grouped / alone / spread  share of alive champion-rounds r1-300 with 2+ / no allies within 4
           (Chebyshev) and mean pairwise ally distance r1-300; medians over sides (O15).
  1st buy  first purchase round, median over champions of sides that reached r400; emblem #1-#3
           rounds likewise (O5: T1 r112, emblems r226/358/459; us r132, r293/449/564).
  H/S/F    share of the shards bought by r400 that are Health / Strength / Focus, per side with
           >= 5 shards, median over sides (O4: T1 Health 52%, us 15%).
  emblems  emblems completed by the team at r200 / r300 / r400, mean per side.
  bags     tribute bag sizes pooled over all tributes: median, p75, p90, median of those by r400,
           share of banked gems in bags >= 50 (O11: T1 24/44/66, 45%; us 22/35/44, 26%).
  K/D400   kills, champion-caused deaths and camp deaths by r400, mean per side.
  win r    median round of games won by reaching 1000 banked gems (O7: T1-T1 r1085).
--online prints online.md's reference rows (built by --online-reference from the foundation's
cached tables into online_reference.json beside this file) under the local rows.
Decoded games are cached in .cache/contenders/battery/analysis/ by replay hash; a change to
onlinedefs.py invalidates the cache. Read-only over runs; no server access.
"""
from __future__ import annotations

import argparse
import collections
import concurrent.futures as cf
import fnmatch
import gzip
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "arena"))
import onlinedefs as od  # noqa: E402
from onlinedefs import mean, median, quant  # noqa: E402

CACHE = ROOT / ".cache" / "contenders" / "battery"
ANALYSIS = CACHE / "analysis"
REFERENCE = HERE / "online_reference.json"
FOUNDATION_CACHE = ROOT / ".cache" / "contenders" / "online"
NODE = Path.home() / ".cache/codex-runtimes/codex-primary-runtime/dependencies/node/bin/node.exe"
INSPECT = ROOT / "tools" / "inspect-strategy-replay.mjs"
DEF_VERSION = hashlib.sha256((HERE / "onlinedefs.py").read_bytes()).hexdigest()[:12]
CLS = ("camp", "home", "burst", "chase", "picked", "outnumbered", "duel", "brawl")
ENDS = ("separation", "recall", "death", "game_end")
STATS = ("Health", "Strength", "Focus")
CAPS = ("wolf", "elk", "golem", "monolith", "gem", "gold", "mix")


# ----------------------------------------------------------------------------- per-game analysis
def decode_tracks(replay: Path) -> dict:
    """Tracks of every champion in the foundation's compact form {map, w, h, tracks: [{id, team,
    type, s: [[round, x, y, hp, gold, gems], ...]}]} (positions are identical with captures off)."""
    cmd = [str(NODE), "--disable-warning=ExperimentalWarning", str(INSPECT), "--replay", str(replay),
           "--actor", "all", "--captures", "0", "--max-steps", "10000000"]
    try:
        from core import Slots  # the machine-wide slot pool shared with every arena process
        slots = Slots(1, quiet=True)
    except Exception:  # noqa: BLE001  (outside the repository layout: no pool)
        slots = None
    if slots:
        with slots:
            p = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=900)
    else:
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=900)
    if p.returncode:
        err = [l for l in p.stderr.decode("utf-8", "replace").splitlines() if l.strip()]
        raise RuntimeError("replay decode failed: " + (err[-1] if err else "no output")[:200])
    d = json.loads(p.stdout.decode("utf-8"))
    if d.get("stopped") not in ("timeline_end", "round_range_end"):
        raise RuntimeError(f"replay decode stopped early ({d.get('stopped')})")
    return {"map": d.get("mapId"), "w": d.get("width"), "h": d.get("height"),
            "tracks": [{"id": t["id"], "team": t["team"], "type": t["type"], "s": t["samples"]} for t in d["tracks"]]}


def _sha(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def stats_file(replay: Path) -> Path | None:
    """The game's statistics beside its replay: <name>.json, or <name>.json.gz once compacted."""
    for p in (replay.with_suffix(".json"), replay.with_suffix(".json.gz")):
        if p.is_file():
            return p
    return None


def read_stats(path: Path) -> dict:
    opener = gzip.open if str(path).endswith(".gz") else open
    with opener(path, "rt", encoding="utf-8") as f:
        return json.load(f)


def compact_stats(replay: Path) -> int:
    """Gzip <name>.json beside a replay into <name>.json.gz (lossless, about a tenth of the size),
    then remove the plain file; returns the bytes saved (0 when nothing to do)."""
    src = replay.with_suffix(".json")
    if not src.is_file():
        return 0
    dst = replay.with_suffix(".json.gz")
    tmp = dst.with_suffix(f".{time.time_ns()}.tmp")
    with open(src, "rb") as fi, gzip.open(tmp, "wb", compresslevel=6) as fo:
        for chunk in iter(lambda: fi.read(1 << 20), b""):
            fo.write(chunk)
    tmp.replace(dst)
    saved = src.stat().st_size - dst.stat().st_size
    src.unlink()
    return saved


def analyse_game(replay: Path, stats_path: Path) -> dict:
    """{sides: [recA, recB], tf: [tfA, tfB] or [], deaths, episodes} of one game, cached by replay hash."""
    key = _sha(replay)
    cache = ANALYSIS / key[:2] / f"{key}.json.gz"
    if cache.is_file():
        try:
            with gzip.open(cache, "rt", encoding="utf-8") as f:
                got = json.load(f)
            if got.get("v") == DEF_VERSION:
                return got
        except (OSError, ValueError):
            pass
    s = read_stats(stats_path)
    tr = decode_tracks(replay)
    mid = replay.stem
    deaths, eps = od.deaths_and_episodes(mid, tr, s)
    out = {"v": DEF_VERSION, "replay_sha": key, "sides": od.side_records(mid, s), "tf": od.track_features(mid, tr, s),
           "deaths": deaths, "episodes": eps}
    out = json.loads(json.dumps(out))  # tuples become lists, as in the foundation's jsonl caches
    cache.parent.mkdir(parents=True, exist_ok=True)
    tmp = cache.with_suffix(f".{time.time_ns()}.tmp")
    with gzip.open(tmp, "wt", encoding="utf-8") as f:
        json.dump(out, f, separators=(",", ":"))
    try:
        tmp.replace(cache)
    except OSError:  # another process holds or just wrote the same game (Windows refuses the replace)
        tmp.unlink(missing_ok=True)
    return out


def bundle(game: dict, team: str) -> dict:
    """One side's records: rec (side record), tf (track features), deaths (as victim), eps (episodes)."""
    rec = next(r for r in game["sides"] if r["team"] == team)
    tf = next((t for t in game["tf"] if t["team"] == team), None)
    return {"rec": rec, "tf": tf, "deaths": [d for d in game["deaths"] if d["victim_team"] == team],
            "eps": [e for e in game["episodes"] if e["team"] == team], "tracked": bool(game["tf"])}


# ----------------------------------------------------------------------------- runs
def _rows(run: Path) -> list[dict]:
    p = run / "games.jsonl" if run.is_dir() else run
    out = []
    for line in p.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except ValueError:
                continue
    return out


def _subject_side(row: dict, subject: str | None) -> str | None:
    if not subject:
        s = str((row.get("tags") or {}).get("subject") or "").upper()
        return s if s in ("A", "B") else None
    if subject.upper() in ("A", "B"):
        return subject.upper()
    hits = []
    for s in "AB":
        key = str(row.get(s.lower()) or "")
        cands = {key, key.split("@")[0], str(row.get(f"{s.lower()}_name") or "")}
        if any(c and (c == subject or fnmatch.fnmatchcase(c, subject)) for c in cands):
            hits.append(s)
    return hits[0] if len(hits) == 1 else None


def load_runs(runs: list[Path], side: str = "subject", subject: str | None = None, workers: int = 6,
              quiet: bool = False) -> tuple[list[dict], dict]:
    """Bundles of the subject's (or the opponent's) side in every finished game with a replay and
    statistics, each tagged with row info: {opponent, map, side, seed, result, row}. Returns
    (bundles, coverage)."""
    todo, cov = [], collections.Counter()
    for run in runs:
        run = Path(run).resolve()
        for r in _rows(run):
            cov["rows"] += 1
            sub = _subject_side(r, subject)
            if sub is None:
                cov["no subject"] += 1
                continue
            if r.get("winner") not in ("A", "B", "draw") or r.get("truncated"):
                cov["unfinished"] += 1
                continue
            rp = r.get("replay")
            if not rp:
                cov["no replay"] += 1
                continue
            replay = Path(rp) if Path(rp).is_absolute() else ROOT / rp
            stats = stats_file(replay) if replay.is_file() else None
            if stats is None:
                cov["no replay"] += 1
                continue
            todo.append((r, sub, replay, stats))
    out = []

    def one(item):
        r, sub, replay, stats = item
        try:
            return item, analyse_game(replay, stats), None
        except Exception as exc:  # noqa: BLE001  (one unreadable game must not stop the run)
            return item, None, f"{type(exc).__name__}: {exc}"
    errors = []
    with cf.ThreadPoolExecutor(max_workers=max(1, workers)) as ex:
        for i, (item, game, err) in enumerate(ex.map(one, todo)):
            r, sub, replay, stats = item
            if err:
                errors.append(f"{replay.name}: {err}")
                cov["undecodable"] += 1
                continue
            team_side = sub if side == "subject" else ("B" if sub == "A" else "A")
            b = bundle(game, "teamA" if team_side == "A" else "teamB")
            tags = r.get("tags") or {}
            other = "b" if sub == "A" else "a"
            opp = tags.get("opponent") or str(r.get(other) or "?").split("@")[0]
            b.update(opponent=str(opp), map=r.get("map"), side=team_side, seed=tags.get("seed") or 0, replay=str(replay),
                     group=tags.get("group"), key=(str(opp), r.get("map"), sub, tags.get("seed") or 0),
                     entrant=r.get(team_side.lower()), opp_entrant=r.get(("b" if team_side == "A" else "a")))
            out.append(b)
            cov["measured"] += 1
            if not quiet and (i + 1) % 200 == 0:
                print(f"  measured {i + 1}/{len(todo)} games", file=sys.stderr, flush=True)
    cov = dict(cov)
    if errors:
        cov["errors"] = errors[:20]
    return out, cov


# ----------------------------------------------------------------------------- aggregation
def dead_rounds(r, R):  # extra.py
    return sum(min(60, R - d["r"]) for d in r["deaths_list"] if d["r"] <= R)


def kb(r, R):  # extra.py
    return sum(1 for k in r["kills_list"] if k["r"] <= R)


def sessions(buys):  # items.py: a shop session is a run of one champion's purchases with gaps <= 3 rounds
    out = []
    for r, name, n, amt, x, y in buys:
        if out and r - out[-1][3] <= 3:
            s = out[-1]
            out[-1] = (s[0], s[1] + amt, s[2] + n, r)
        else:
            out.append((r, amt, n, r))
    return [(a, b, c) for a, b, c, d in out]


def shard_share400(r):  # style.py: H/S/F share of shards bought by r400 (None below 5 shards)
    sh = collections.Counter()
    for c in r["champs"]:
        for rr, name, n, amt, x, y in c["buys"]:
            if " Shard of " in name and rr <= 400:
                sh[name.split(" Shard of ")[1]] += n
    tot = sum(sh.values())
    return {s: (sh[s] / tot if tot >= 5 else None) for s in STATS}


def aggregate(B: list[dict]) -> dict:
    """The foundation's table formulas over side bundles (see the module docstring)."""
    S = [b["rec"] for b in B]
    out: dict = {"sides": len(S), "tracked": sum(1 for b in B if b.get("tf"))}
    # O2 rates (extra.py section 4), r400 and the same formula at r1000
    for R, key in ((400, "rate"), (1000, "rate1000")):
        v = [r for r in S if r["rounds"] >= R]
        x = [(r["earnedGold"][str(R)] / (len(r["champs"]) * R - dead_rounds(r, R)), dead_rounds(r, R),
              100 * r["collectedGems"][str(R)] / (len(r["champs"]) * R - dead_rounds(r, R)),
              100 * 100 * kb(r, R) / max(1, r["earnedGold"][str(R)])) for r in v]
        out[key] = {"n": len(v), "gold_per_alive_round": median([a[0] for a in x]),
                    "gold_per_alive_round_mean": mean([a[0] for a in x]), "dead_rounds": mean([a[1] for a in x]),
                    "gems_per_100": median([a[2] for a in x]), "kill_gold_share": mean([a[3] for a in x])}
    # O13 episodes (deathtab.py)
    E = [e for b in B for e in b["eps"]]
    for e in E:
        e["dur"] = e["r1"] - e["r0"] + 1
        e["fight"] = (e["dmg_done"] + e["dmg_taken"]) > 0
    with_eps = [b for b in B if b["eps"]]
    nch = sum(len(b["rec"]["champs"]) for b in with_eps)
    f = [e for e in E if e["fight"]]
    q = [e for e in E if not e["fight"]]
    ends = collections.Counter(e["end"] for e in f)
    out["episodes"] = {"n": len(E), "fights": len(f), "sides": len(with_eps),
                       "per_champ_game": len(E) / nch if nch else None,
                       "fight_share": len(f) / len(E) if E else None,
                       "p_death_fight": ends["death"] / len(f) if f else None,
                       "p_death_quiet": sum(1 for e in q if e["end"] == "death") / len(q) if q else None,
                       "ends": {k: ends[k] / len(f) for k in ENDS} if f else None,
                       "fight_len": median([e["dur"] for e in f])}
    # O14 death classes (deathtab.py), all deaths and deaths by r400
    D = [d for b in B for d in b["deaths"]]
    for lab, v in (("deaths", D), ("deaths400", [d for d in D if d["round"] <= 400])):
        c = collections.Counter(d["cls"] for d in v)
        out[lab] = {"n": len(v), "cls": {k: c[k] / len(v) for k in CLS} if v else None,
                    "contact": median([d["contact"] for d in v]), "hp0": median([d["hp0"] for d in v]),
                    "attackers": mean([d["attackers"] for d in v]), "allies6": mean([d["allies6"] for d in v]),
                    "bush": sum(1 for d in v if d["from_bush"]) / len(v) if v else None}
    # O5 shop timing (items.py)
    cs = [c for r in S if r["rounds"] >= 400 for c in r["champs"]]
    ss = [sessions(c["buys"]) for c in cs]
    g400 = [s[1] for x in ss for s in x if s[0] <= 400]
    gaps = [b2[0] - a2[0] for x in ss for a2, b2 in zip(x, x[1:]) if b2[0] <= 1000]
    out["shop"] = {"champs": len(cs), "first_buy": median([c["buys"][0][0] if c["buys"] else None for c in cs]),
                   "emblem_rounds": [median([c["emblems"][i][0] if len(c["emblems"]) > i else None for c in cs])
                                     for i in range(3)],
                   "no_emblem_by_end": sum(1 for c in cs if not c["emblems"]) / len(cs) if cs else None,
                   "sessions400": mean([sum(1 for s in x if s[0] <= 400) for x in ss]),
                   "gold_per_session400": median(g400), "gap": median(gaps),
                   "side_first_buy": median([median([c["buys"][0][0] if c["buys"] else None for c in r["champs"]])
                                             for r in S])}
    for R in (100, 200, 400):  # items.py: team gold spent / earned by round
        w = [r for r in S if r["rounds"] >= R and r["earnedGold"][str(R)] > 0]
        out["shop"][f"spent_ratio{R}"] = median([r["spentGold"][str(R)] / r["earnedGold"][str(R)] for r in w])
    out["emblems"] = {str(R): mean([sum(1 for c in r["champs"] for e in c["emblems"] if e[0] <= R)
                                    for r in S if r["rounds"] >= R]) for R in (200, 300, 400)}
    # O4 builds (style.py)
    sh = [shard_share400(r) for r in S]
    out["build"] = {s: median([x[s] for x in sh]) for s in STATS}
    out["build"]["n"] = sum(1 for x in sh if x["Health"] is not None)
    fe = collections.Counter(c["emblems"][0][1].replace(" Emblem of ", "/") for r in S for c in r["champs"] if c["emblems"])
    out["build"]["first_emblem"] = {k: v / sum(fe.values()) for k, v in fe.most_common(6)} if fe else {}
    # O11 bags (econ.py)
    bags = [tr[1] for r in S for tr in r["tributes"]]
    early = [tr[1] for r in S for tr in r["tributes"] if tr[0] <= 400]
    out["bags"] = {"n": len(bags), "median": median(bags), "p75": quant(bags, 0.75), "p90": quant(bags, 0.9),
                   "median_r400": median(early), "share_ge50": sum(b for b in bags if b >= 50) / max(1, sum(bags)),
                   "per_side": len(bags) / len(S) if S else None,
                   "carry300": median([r["gems"]["300"] for r in S if r["rounds"] >= 300]),
                   "lost_to_kills": mean([r["gems_lost_champ"] for r in S if r["rounds"] >= 300])}
    # kills, deaths, camp deaths by r400 (style.py)
    out["kd400"] = {"kills": mean([sum(1 for k in r["kills_list"] if k["r"] <= 400) for r in S]),
                    "deaths": mean([sum(1 for d in r["deaths_list"] if d["r"] <= 400 and d["ctx"] == "champion") for r in S]),
                    "camp_deaths": mean([sum(1 for d in r["deaths_list"] if d["r"] <= 400 and d["ctx"] == "camp") for r in S]),
                    "first_cdmg": median([r["first_cdmg"] for r in S])}
    # O15 cohesion (trackfeat.py, extra.py section 8)
    T = [b["tf"] for b in B if b.get("tf")]
    out["cohesion"] = {"n": len(T), "grouped": median([t["coh300"][2] if t.get("coh300") else None for t in T]),
                       "alone": median([t["coh300"][0] if t.get("coh300") else None for t in T]),
                       "spread": median([t.get("spread300") for t in T]), "party": median([t.get("party") for t in T]),
                       "recalls400": median([t.get("recalls400") for t in T]),
                       "enemyhalf300": median([t.get("enemyhalf300") for t in T]),
                       "contact_frac": median([t.get("contact_frac") for t in T]),
                       "near400": median([t.get("near400") for t in T])}
    # captures by camp type by r400 (style.py caps; categories of common.group_cat)
    c400 = [collections.Counter(c[1] for c in r["caps"] if c[0] <= 400) for r in S]
    out["caps400"] = {k: mean([c[k] for c in c400]) for k in CAPS}
    out["caps400"]["total"] = mean([sum(c.values()) for c in c400])
    # outcome and O7 round of victory
    res = collections.Counter(r["res"] for r in S)
    won = [r["rounds"] for r in S if r["res"] == "win" and r["gems_final"] >= 1000]
    out["outcome"] = {"w": res["win"], "l": res["loss"], "d": res["draw"], "win_round": median(won),
                      "win_round_q": [quant(won, 0.25), quant(won, 0.75)], "wins_by_gems": len(won),
                      "gems": mean([r["gems_final"] for r in S]), "opp_gems": mean([r["ogems_final"] for r in S])}
    picks = collections.Counter(ch for r in S for ch in set(r["draft"]))
    out["draft"] = {k: v / len(S) for k, v in picks.most_common()} if S else {}
    return out


# ----------------------------------------------------------------------------- online reference
def _load_jsonl(path: Path) -> list[dict]:
    with open(path, encoding="utf-8") as f:
        return [json.loads(line) for line in f if line.strip()]


def online_bundles() -> dict:
    """(mid, team) -> bundle from the foundation's cached tables (sides, trackfeat, deaths, episodes)."""
    sides = {(r["mid"], r["team"]): r for r in _load_jsonl(FOUNDATION_CACHE / "sides.jsonl")}
    tf = {(t["mid"], t["team"]): t for t in _load_jsonl(FOUNDATION_CACHE / "trackfeat.jsonl")}
    B = {k: {"rec": r, "tf": tf.get(k), "deaths": [], "eps": [], "tracked": k in tf} for k, r in sides.items()}
    for d in _load_jsonl(FOUNDATION_CACHE / "deaths.jsonl"):
        b = B.get((d["mid"], d["victim_team"]))
        if b:
            b["deaths"].append(d)
    for e in _load_jsonl(FOUNDATION_CACHE / "episodes.jsonl"):
        b = B.get((e["mid"], e["team"]))
        if b:
            b["eps"].append(e)
    return B


def build_reference() -> dict:
    B = online_bundles()
    v = list(B.values())

    def us(vers=None, otiers=None):
        return [b for b in v if b["rec"]["tier"] == "US" and (vers is None or b["rec"].get("version") in vers)
                and (otiers is None or b["rec"].get("otier") in otiers)]
    rows = {t: [b for b in v if b["rec"]["tier"] == t] for t in ("T1", "T2", "T3", "T4", "T5")}
    rows["us (v1-v36)"] = us()
    rows["us v33-v36"] = us({"33", "34", "35", "36"})
    rows["us v35"] = us({"35"})
    rows["us v36"] = us({"36"})
    for lab, ot in (("vs T1", {"T1"}), ("vs T2", {"T2"}), ("vs T3", {"T3"}), ("vs T4-T5", {"T4", "T5"})):
        rows[f"us v35 {lab}"] = us({"35"}, ot)
    ref = {"built": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "definitions": DEF_VERSION,
           "source": "foundation caches .cache/contenders/online/{sides,trackfeat,deaths,episodes}.jsonl "
                     "(online.md Coverage: 1,126 summaries 21-22 Oct, engine 1.0.2; tiers from the 22 Oct 03:2x ladder)",
           "rows": {k: aggregate(x) for k, x in rows.items()}}
    return ref


def load_reference() -> dict | None:
    try:
        return json.loads(REFERENCE.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


# ----------------------------------------------------------------------------- printing
def _f(x, fmt="{:.2f}", none="-"):
    return none if x is None else fmt.format(x)


def _pct(x):
    return "-" if x is None else f"{100 * x:.0f}%"


def row_cells(a: dict) -> list[str]:
    """Compact cells of one aggregate (the columns of HEADER)."""
    ep, d, sh, co, kd, bg = a["episodes"], a["deaths"], a["shop"], a["cohesion"], a["kd400"], a["bags"]
    ends = ep["ends"] or {}
    b = a["build"]
    emb = a["emblems"]
    return [str(a["sides"]), _f(a["rate"]["gold_per_alive_round"]), _f(a["rate1000"]["gold_per_alive_round"]),
            _f(a["rate"]["dead_rounds"], "{:.0f}"), _f(a["rate"]["gems_per_100"], "{:.1f}"),
            _pct(ep["p_death_fight"]) + f" ({ep['fights']})",
            f"{_pct(ends.get('separation'))}/{_pct(ends.get('recall'))}",
            f"{_f(co['grouped'])}/{_f(co['alone'])}/{_f(co['spread'], '{:.1f}')}",
            _f(sh["first_buy"], "r{:.0f}"),
            "/".join(_f(b[s], "{:.0%}") for s in STATS),
            "/".join(_f(emb.get(k), "{:.1f}") for k in ("200", "300", "400")),
            f"{_f(bg['median'], '{:.0f}')}/{_f(bg['p90'], '{:.0f}')}",
            f"{_f(kd['kills'], '{:.1f}')}/{_f(kd['deaths'], '{:.1f}')}",
            _f(a["outcome"]["win_round"], "r{:.0f}")]


HEADER = ["sides", "gold400", "gold1000", "dead400", "gems/100", "P(d|f) (fights)", "sep/rec", "grp/alone/spread",
          "1st buy", "H/S/F r400", "emb r200/300/400", "bag med/p90", "K/D r400", "win r"]


def table(header: list[str], rows: list[list[str]]) -> list[str]:
    widths = [max(len(str(r[i])) for r in [header] + rows) for i in range(len(header))]
    return ["  ".join(str(c).ljust(w) for c, w in zip(r, widths)).rstrip() for r in [header] + rows]


def class_line(a: dict) -> str:
    d = a["deaths"]
    if not d["n"]:
        return "no champion-caused deaths"
    return (f"{d['n']} deaths: " + " ".join(f"{k} {100 * d['cls'][k]:.0f}" for k in CLS)
            + f"; contact r{_f(d['contact'], '{:.0f}')} from hp {_f(d['hp0'])}")


def report(groups: dict, ref: dict | None = None, ref_rows=("us v35", "us (v1-v36)", "T1", "T2")) -> list[str]:
    rows = [[g] + row_cells(a) for g, a in groups.items()]
    if ref:
        rows += [[f"online {k}"] + row_cells(ref["rows"][k]) for k in ref_rows if k in ref["rows"]]
    return table(["group"] + HEADER, rows)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("runs", nargs="*", help="arena run/sweep/battery directories (games.jsonl) or .jsonl logs")
    p.add_argument("--by-opponent", action="store_true")
    p.add_argument("--by-map", action="store_true")
    p.add_argument("--side", choices=("subject", "opponent"), default="subject",
                   help="measure the subject's side (default) or its opponents' sides in the same games")
    p.add_argument("--subject", default=None, help="A|B or a bot id/entrant glob for rows without tags.subject")
    p.add_argument("--online", action="store_true", help="add online.md's reference rows (us v35, us, T1, T2)")
    p.add_argument("--json", default="", help="write the aggregates and coverage here")
    p.add_argument("--workers", type=int, default=6, help="parallel replay decodes (each takes an arena slot)")
    p.add_argument("--compact", action="store_true",
                   help="after measuring, gzip each measured game's statistics JSON (lossless, ~10x smaller)")
    p.add_argument("--online-reference", action="store_true",
                   help=f"rebuild {REFERENCE.name} from the foundation's cached online tables, then exit")
    a = p.parse_args()
    if a.online_reference:
        ref = build_reference()
        REFERENCE.write_text(json.dumps(ref, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        print(f"wrote {REFERENCE.relative_to(ROOT).as_posix()} ({len(ref['rows'])} rows)")
        print("\n".join(report({}, ref, tuple(ref["rows"]))))
        return 0
    if not a.runs:
        p.error("give at least one run directory (or --online-reference)")
    B, cov = load_runs([Path(r) for r in a.runs], a.side, a.subject, a.workers)
    if not B:
        print(f"no measurable games ({cov}); runs need --keep-replays all")
        return 1
    groups = {"all": aggregate(B)}
    if a.by_opponent:
        for o in sorted({b["opponent"] for b in B}):
            groups[f"vs {o}"] = aggregate([b for b in B if b["opponent"] == o])
    if a.by_map:
        for m in sorted({b["map"] for b in B}):
            groups[m] = aggregate([b for b in B if b["map"] == m])
    ref = load_reference() if a.online else None
    ents = sorted({str(b["entrant"]) for b in B})
    print(f"{a.side} side: {', '.join(ents[:3])}{' ...' if len(ents) > 3 else ''}; coverage "
          + ", ".join(f"{k} {v}" for k, v in cov.items() if k != "errors"))
    print("\n".join(report(groups, ref)))
    print("death classes (all): " + class_line(groups["all"]))
    if a.compact:
        saved = sum(compact_stats(Path(b["replay"])) for b in B)
        print(f"compacted statistics: {saved / 2 ** 20:.0f} MiB saved")
    if a.json:
        Path(a.json).write_text(json.dumps({"runs": a.runs, "side": a.side, "coverage": cov, "definitions": DEF_VERSION,
                                            "groups": groups}, indent=1) + "\n", encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())

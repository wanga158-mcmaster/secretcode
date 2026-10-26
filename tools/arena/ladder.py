"""Simulated local ladder: one continuous Bradley-Terry league of every bot version, which any agent
can extend at any time.

  arena ladder run [SEL] [--pairings N] [--maps pool] [--jobs N] [--loop [--max-loops K]]
      Build the current version of each selected bot (failed builds are left out and logged to
      results/ladder/builds.jsonl), schedule N informative pairings never played before (default 2
      per entrant), play them, refit the ratings, write the leaderboard and print its top.
      --loop repeats until interrupted, re-preparing the entrants each time (new versions join).
  arena ladder table [SEL] [--all-versions] [--top N]
      Rank, Elo +- 2 se, games, score, W-L-D, health (faults/exhausted/errors/stalls) and style
      (mean gems, gold, kills, deaths, clears per game) of the current versions (every version
      with --all-versions). Also writes results/ladder/leaderboard.md (all current versions).
  arena ladder styles [SEL] [--threshold D] [--min-games N] [--top N]
      Style vector per current version (gems, gold, kills, deaths, clears, opponent gems, rounds
      per game, z-normalized over all current versions), each bot's nearest neighbour among the
      other bots and their head-to-head. Close styles with a near-even head-to-head are flagged as
      possible convergence: the non-convergence check of the suites.
  arena ladder vs A B [--all-versions]
      Head-to-head record of two bots (current versions unless --all-versions; A or B may also be
      an exact key <suite>/<bot>@<version>).

Model. Every version (content hash of the build closure, core.version) is its own player; old
versions stay in the fit as fixed points, so ratings remain comparable over time. Games are
deterministic, so a game key (team A key, team B key, map) is played once and duplicates count
once. Ratings: stats.bradley_terry over the completed games (win 1, draw 0.5, loss 0; error and
skipped rows ignored), anchored so the newest anchors/starter-cpp version sits at 1000 (without
starter games the mean is 1500). Only rows of the pinned engine count (tags.engine; rows without
the tag count as current), so an engine upgrade restarts the ratings and keeps the history.

Scheduler (deterministic given the log and the arguments). A pairing is two games on one map with
sides swapped; no game key is ever replayed (an errored game is retried once). 1. Placement:
entrants with fewer than 20 game attempts go first, fewest first, against opponents spread over the
rating quantiles (about 10 pairings). 2. Style: entrants whose styles are nearest neighbours
(distance < 0.25, as in `styles`) but never met get one pairing, so the convergence check has a
head-to-head. 3. Information: pairs chosen greedily by p(1-p) * (se_a + se_b), i.e. close ratings
weighted by uncertainty (se shrinks as games are scheduled), x1.25 for opponents from another
suite, x1/(1 + 0.5 m) for a pair that already met on m maps. A pairing takes the map both entrants
have played least (rotating through --maps) and falls back to the missing half of a half-played
map. No entrant gets more than ~3x the mean share of the pairings of a run. Concurrent runs claim
their games under a lock (.cache/arena/ladder/claims/), so two agents never schedule one game.

Files: results/ladder/games.jsonl (append-only rows from games.play with tags {"ladder": true,
"engine": <version>, "phase": "place"|"style"|"info"}), results/ladder/builds.jsonl ({ts, key, bot,
error}), results/ladder/leaderboard.md, .cache/arena/ladder/<time>/ (run outputs; replays of
flagged games only).

Library: load, played, fit, schedule, standings, styles, head_to_head, near, current_keys.
"""
from __future__ import annotations

import hashlib
import heapq
import itertools
import json
import math
import os
import statistics
import sys
import time
from collections import Counter, defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from core import (CACHE, RESULTS, ROOT, ArenaError, Bot, FileLock, all_bots, append_jsonl,  # noqa: E402
                  map_set, prepare_many, read_jsonl, select, version)
from core import bot as find_bot  # noqa: E402
from games import GameSpec, flagged, health, perspective, play  # noqa: E402
from health_context import finalize_ladder_run  # noqa: E402
from stats import ELO, bradley_terry, expected  # noqa: E402

LADDER = RESULTS / "ladder"
LOG = LADDER / "games.jsonl"
BUILDS = LADDER / "builds.jsonl"
BOARD = LADDER / "leaderboard.md"
RUNS = CACHE / "ladder"
CLAIMS = RUNS / "claims"
STARTER = "anchors/starter-cpp"
ANCHOR_ELO = 1000.0
PLAYED = ("A", "B", "draw")
FAILED = ("error", "skipped")
PRIOR = 2.0                       # virtual half-won games per player in the fit (bradley_terry default)
SE0 = ELO / math.sqrt(PRIOR / 4)  # se of a player without games
PLACE_GAMES = 20                  # placement until 20 game attempts (about 10 pairings)
SPREAD = (0.5, 0.15, 0.85, 0.35, 0.65, 0.0, 1.0, 0.25, 0.75, 0.5)  # rating quantiles of placement opponents
BAND = 0.1                        # placement accepts any opponent within 0.1 of the target quantile
CAP_FACTOR = 3.0                  # pairings per entrant and run: at most ~3x the mean share
SUITE_BONUS = 1.25                # information bonus for opponents from another suite
REPEAT_DECAY = 0.5                # information x 1 / (1 + 0.5 * maps the pair already met on)
MAX_ATTEMPTS = 2                  # a game key that errored twice is not scheduled again
STYLE = ("gems", "gold", "kills", "deaths", "clears", "opp_gems", "rounds")
STYLE_THRESHOLD = 0.25            # RMS z-distance below which two styles count as close
STYLE_MIN_GAMES = 4               # games a version needs before its style is compared
IDLE_WAIT = 300                   # --loop: seconds to wait when nothing new can be scheduled
_IDS = itertools.count(1)


def engine_version() -> str:
    try:
        doc = json.loads((ROOT / "docs" / "reference" / "current-version.json").read_text(encoding="utf-8"))
        return str(doc["engine"]["version"])
    except (OSError, ValueError, KeyError, TypeError):
        return "unknown"


ENGINE = engine_version()


# ----------------------------------------------------------------------------- small helpers
def _now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _h(text: str) -> int:
    """Stable tie-break value (Python's hash() of a str changes between processes)."""
    return int(hashlib.sha256(text.encode()).hexdigest()[:12], 16)


def _int(v) -> int:
    try:
        return int(v)
    except (TypeError, ValueError):
        try:
            return int(float(v))
        except (TypeError, ValueError):
            return 0


def bot_of(key: str) -> str:
    """Bot id of an entrant key <suite>/<bot>@<version>."""
    return str(key).rsplit("@", 1)[0]


def version_of_key(key: str) -> str:
    return str(key).rsplit("@", 1)[1] if "@" in str(key) else ""


def suite_of(key: str) -> str:
    return str(key).split("/", 1)[0]


def _pair(x: str, y: str) -> tuple[str, str]:
    return (x, y) if x < y else (y, x)


def _score(winner: str) -> float:
    """Team A's score."""
    return 1.0 if winner == "A" else 0.0 if winner == "B" else 0.5


def _tail(text: str, lines: int = 20, limit: int = 3000) -> str:
    kept = [l.rstrip() for l in (text or "").strip().splitlines() if l.strip()][-lines:]
    return "\n".join(kept)[-limit:]


def _brief(text: str, limit: int = 160) -> str:
    lines = [l.strip() for l in str(text or "").splitlines() if l.strip()] or ["?"]
    s = lines[0] if len(lines) == 1 else f"{lines[0]} ... {lines[-1]}"
    return s if len(s) <= limit else s[:limit - 3] + "..."


def _is_all(spec) -> bool:
    return str(spec or "all").strip() in ("", "all")


# ----------------------------------------------------------------------------- log and ratings
def load(path: Path | None = None, engine: str | None = None) -> list[dict]:
    """Ladder rows in log order, read under the log's writer lock. Only rows of `engine` (default:
    the pinned engine; "*" = all) are returned; rows without an engine tag count as current."""
    path = Path(path or LOG)
    if not path.is_file():
        return []
    with FileLock(path.with_name(path.name + ".lock")):
        rows = read_jsonl(path)
    eng = ENGINE if engine is None else engine
    out = []
    for r in rows:
        if not isinstance(r, dict) or not all(isinstance(r.get(k), str) for k in ("a", "b", "map", "winner")):
            continue
        tags = r.get("tags") if isinstance(r.get("tags"), dict) else {}
        if eng == "*" or (tags.get("engine") or r.get("engine")) in (None, eng):
            out.append(r)
    return out


def played(rows: list[dict]) -> list[dict]:
    """Completed games (winner A, B or draw, not truncated, two different keys): the first row of
    each game key (a, b, map); later duplicates are dropped."""
    seen, out = set(), []
    for r in rows:
        if r.get("winner") in PLAYED and not r.get("truncated") and r.get("a") != r.get("b"):
            k = (r.get("a"), r.get("b"), r.get("map"))
            if k not in seen:
                seen.add(k)
                out.append(r)
    return out


def conflicts(rows: list[dict]) -> int:
    """Game keys whose completed duplicates disagree on the winner (games should be deterministic)."""
    winners: dict[tuple, set] = defaultdict(set)
    for r in rows:
        if r.get("winner") in PLAYED and not r.get("truncated"):
            winners[(r.get("a"), r.get("b"), r.get("map"))].add(r["winner"])
    return sum(1 for w in winners.values() if len(w) > 1)


def anchor_key(games: list[dict]) -> str | None:
    """The newest anchors/starter-cpp version (latest first appearance) among completed games."""
    first: dict[str, str] = {}
    for r in games:
        for k in (r["a"], r["b"]):
            if bot_of(k) == STARTER:
                ts = str(r.get("ts") or "")
                if k not in first or ts < first[k]:
                    first[k] = ts
    return max(first, key=lambda k: (first[k], k)) if first else None


def fit(rows: list[dict], anchor: str | None = None) -> dict:
    """Bradley-Terry ratings of every key with completed games: {key: {elo, se, games, score}},
    the newest starter version (or `anchor`) at 1000."""
    games = played(rows)
    anchor = anchor or anchor_key(games)
    return bradley_terry([(r["a"], r["b"], _score(r["winner"])) for r in games], prior=PRIOR,
                         anchor=anchor, anchor_elo=ANCHOR_ELO)


# ----------------------------------------------------------------------------- scheduler
@dataclass
class Pairing:
    x: str
    y: str
    map: str
    games: list   # [(team A key, team B key)]: both side orders, or the one still missing
    phase: str    # "place" | "style" | "info"


def cap_for(pairings: int, entrants: int) -> int:
    """Most pairings one entrant takes part in during one run: about 3x the mean share 2N/n."""
    return max(1, math.ceil(CAP_FACTOR * 2 * pairings / max(1, entrants) - 1e-9))


class _Scheduler:
    def __init__(self, keys: list[str], rows: list[dict], maps: list[str], pairings: int, ratings: dict,
                 taken):
        self.keys, self.cur, self.maps = keys, set(keys), maps
        self.cap = cap_for(pairings, len(keys))
        self.done: set[tuple] = set()      # game keys never to schedule again
        errors: Counter = Counter()
        for r in rows:
            g = (r.get("a"), r.get("b"), r.get("map"))
            if r.get("winner") in PLAYED and not r.get("truncated"):
                self.done.add(g)
            elif r.get("winner") in FAILED:
                errors[g] += 1
        self.done |= {g for g, n in errors.items() if n >= MAX_ATTEMPTS}
        self.done |= {tuple(g) for g in taken}
        self.attempts: Counter = Counter()   # distinct game keys tried per entrant key
        self.met: dict[str, set] = defaultdict(set)
        self.mapuse: dict[str, Counter] = defaultdict(Counter)
        self.mapall: Counter = Counter()
        met_on: dict[tuple, set] = defaultdict(set)
        for a, b, m in self.done | set(errors):
            self.attempts[a] += 1
            self.attempts[b] += 1
            self.mapuse[a][m] += 1
            self.mapuse[b][m] += 1
            self.mapall[m] += 1
            self.met[a].add(b)
            self.met[b].add(a)
            if a in self.cur and b in self.cur:
                met_on[_pair(a, b)].add(m)
        self.meetings = Counter({p: len(ms) for p, ms in met_on.items()})
        rated = {k: ratings[k] for k in keys if k in ratings}
        default = statistics.median(v["elo"] for v in rated.values()) if rated else 1500.0
        self.elo = {k: float(rated[k]["elo"]) if k in rated else default for k in keys}
        self.info = {k: (ELO / rated[k]["se"]) ** 2 if k in rated and rated[k].get("se") else (ELO / SE0) ** 2
                     for k in keys}
        order = sorted(rated, key=lambda k: (rated[k]["elo"], k))
        self.quantile = {k: (i / (len(order) - 1) if len(order) > 1 else 0.5) for i, k in enumerate(order)}
        self.used: Counter = Counter()      # pairings scheduled this run
        self.stamp: Counter = Counter()     # bumps whenever an entrant's state changes
        self.stuck: set[str] = set()
        self.plan: list[Pairing] = []

    def se(self, k: str) -> float:
        return ELO / math.sqrt(self.info[k])

    def options(self, x: str, y: str):
        """(map, games) for the next pairing of x and y, or None when they have played everything."""
        n = len(self.maps)
        off = _h("|".join(_pair(x, y))) % n
        best = None
        for i, m in enumerate(self.maps):
            left = [(a, b) for a, b in ((x, y), (y, x)) if (a, b, m) not in self.done]
            if not left:
                continue
            rank = (len(left) != 2, self.mapuse[x][m] + self.mapuse[y][m], self.mapall[m], (i - off) % n)
            if best is None or rank < best[0]:
                best = (rank, m, left)
        return None if best is None else (best[1], best[2])

    def add(self, x: str, y: str, m: str, left: list, phase: str) -> None:
        for a, b in left:
            self.done.add((a, b, m))
            self.mapuse[a][m] += 1
            self.mapuse[b][m] += 1
            self.mapall[m] += 1
        g = len(left)
        p = expected(self.elo[x], self.elo[y])
        for k in (x, y):
            self.attempts[k] += g
            self.info[k] += g * p * (1 - p)
            self.used[k] += 1
            self.stamp[k] += 1
        self.met[x].add(y)
        self.met[y].add(x)
        self.meetings[_pair(x, y)] += 1
        self.plan.append(Pairing(x, y, m, list(left), phase))

    def placement(self, x: str):
        """Opponent and map for the next placement pairing of x: a rating quantile within BAND of the
        next SPREAD target, preferring new opponents and other suites, then the opponents least used
        in this run and with the fewest games (so simultaneous placements share the load)."""
        q = SPREAD[len(self.met[x]) % len(SPREAD)]

        def cost(y):
            c = max(0.0, abs(self.quantile.get(y, 0.5) - q) - BAND)
            c += 0.25 if y in self.met[x] else 0.0
            c += 0.05 if suite_of(y) == suite_of(x) else 0.0
            return round(c, 9), self.used[y], self.attempts[y], _h(f"{x}|{y}")
        for y in sorted((y for y in self.keys if y != x and self.used[y] < self.cap), key=cost):
            opt = self.options(x, y)
            if opt:
                return y, opt
        return None

    def gain(self, x: str, y: str) -> float:
        p = expected(self.elo[x], self.elo[y])
        g = p * (1 - p) * (self.se(x) + self.se(y))
        if suite_of(x) != suite_of(y):
            g *= SUITE_BONUS
        return g / (1 + REPEAT_DECAY * self.meetings[_pair(x, y)])

    def run(self, pairings: int, neighbours=()) -> list[Pairing]:
        while len(self.plan) < pairings:
            todo = [k for k in self.keys
                    if self.attempts[k] < PLACE_GAMES and self.used[k] < self.cap and k not in self.stuck]
            if not todo:
                break
            x = min(todo, key=lambda k: (self.attempts[k], _h(k)))
            pick = self.placement(x)
            if pick is None:
                self.stuck.add(x)
                continue
            self.add(x, pick[0], *pick[1], "place")
        for x, y in neighbours:  # close styles that never met: the convergence check needs their record
            if len(self.plan) >= pairings:
                break
            if self.used[x] < self.cap and self.used[y] < self.cap and not self.meetings[_pair(x, y)]:
                opt = self.options(x, y)
                if opt:
                    self.add(x, y, *opt, "style")
        if len(self.plan) >= pairings:
            return self.plan
        # lazy greedy: gains only shrink, so a fresh entry at the top of the heap is the true best
        heap = [(-self.gain(x, y), _h(f"{x}|{y}"), x, y, self.stamp[x], self.stamp[y])
                for x, y in itertools.combinations(self.keys, 2)]
        heapq.heapify(heap)
        while heap and len(self.plan) < pairings:
            _, tie, x, y, sx, sy = heapq.heappop(heap)
            if self.used[x] >= self.cap or self.used[y] >= self.cap:
                continue
            if (sx, sy) != (self.stamp[x], self.stamp[y]):
                heapq.heappush(heap, (-self.gain(x, y), tie, x, y, self.stamp[x], self.stamp[y]))
                continue
            opt = self.options(x, y)
            if opt:
                self.add(x, y, *opt, "info")
                heapq.heappush(heap, (-self.gain(x, y), tie, x, y, self.stamp[x], self.stamp[y]))
        return self.plan


def schedule(keys: list[str], rows: list[dict], maps: list[str], pairings: int, ratings: dict | None = None,
             taken=()) -> list[Pairing]:
    """Up to `pairings` new pairings among the entrant keys (current versions), deterministic given
    the rows, maps and arguments. `taken` holds (a, b, map) game keys claimed by concurrent runs."""
    keys = sorted(set(keys))
    maps = list(dict.fromkeys(maps))
    if len(keys) < 2 or pairings <= 0 or not maps:
        return []
    ratings = fit(rows) if ratings is None else ratings
    return _Scheduler(keys, rows, maps, pairings, ratings, taken).run(pairings, style_pairs(rows, keys))


def style_pairs(rows: list[dict], keys: list[str]) -> list[tuple[str, str]]:
    """Entrant pairs whose styles are nearest neighbours closer than STYLE_THRESHOLD (z over the
    entrants) and that never met, closest first."""
    out: list[tuple[str, str]] = []
    for s in styles(rows, {bot_of(k): k for k in keys}, STYLE_MIN_GAMES, STYLE_THRESHOLD):
        if s["flag"] == "close style, no head-to-head":
            p = _pair(s["key"], s["nearest"])
            if p not in out:
                out.append(p)
    return out


# ----------------------------------------------------------------------------- claims of running games
def _held_elsewhere(lock: Path) -> bool:
    try:
        with FileLock(lock, poll=0.01, timeout=0):
            return False
    except ArenaError:
        return True


def _unlink(p: Path) -> None:
    try:
        p.unlink(missing_ok=True)
    except OSError:
        pass


class Claim:
    """Game keys a running ladder process is playing; held by a lock for the process's lifetime, so
    claims of a process that died are recognised as stale and dropped."""

    def __init__(self, games: list[list[str]]):
        CLAIMS.mkdir(parents=True, exist_ok=True)
        ident = f"{time.strftime('%Y%m%dT%H%M%S')}-{os.getpid()}-{next(_IDS)}"
        self.lock_path, self.file = CLAIMS / f"{ident}.lock", CLAIMS / f"{ident}.json"
        self.lock = FileLock(self.lock_path)
        self.lock.__enter__()
        self.file.write_text(json.dumps({"pid": os.getpid(), "ts": _now(), "games": games}), encoding="utf-8")

    def release(self) -> None:
        if self.lock is None:
            return
        _unlink(self.file)
        self.lock.__exit__(None, None, None)
        self.lock = None
        _unlink(self.lock_path)


def live_claims() -> set[tuple]:
    """Game keys claimed by live ladder runs; stale claims (owner gone) are removed."""
    out: set[tuple] = set()
    if not CLAIMS.is_dir():
        return out
    for f in sorted(CLAIMS.glob("*.json")):
        lock = f.with_suffix(".lock")
        if not _held_elsewhere(lock):
            _unlink(f)
            _unlink(lock)
            continue
        try:
            out |= {tuple(g) for g in json.loads(f.read_text(encoding="utf-8")).get("games", [])}
        except (OSError, ValueError, AttributeError, TypeError):
            continue
    return out


# ----------------------------------------------------------------------------- standings and board
def current_keys(bots: list[Bot] | None = None) -> dict[str, str]:
    """{bot id: <suite>/<bot>@<version of the working tree>} for the bots (default: all)."""
    out = {}
    for b in (all_bots() if bots is None else bots):
        try:
            out[b.id] = f"{b.id}@{version(b)}"
        except OSError:
            continue  # a file vanished while another agent edits the bot
    return out


def _blank() -> dict:
    return {"games": 0, "wins": 0, "losses": 0, "draws": 0, "faults": 0, "exhausted": 0, "errors": 0,
            "stalls": 0, "failed": 0, "last": "", **{f"sum_{k}": 0 for k in STYLE}}


def tally(rows: list[dict]) -> dict[str, dict]:
    """Per entrant key over its completed games: games, wins, losses, draws, faults, exhausted,
    errors, stalls, sum_<style key>, last (latest ts); failed = its error/skipped rows."""
    out: dict[str, dict] = defaultdict(_blank)
    for r in played(rows):
        for side, other in (("A", "B"), ("B", "A")):
            me, op = r.get(side) or {}, r.get(other) or {}
            t = out[r[side.lower()]]
            t["games"] += 1
            w = r["winner"]
            t["wins" if w == side else "losses" if w == other else "draws"] += 1
            for k in ("faults", "exhausted", "errors"):
                t[k] += _int(me.get(k))
            t["stalls"] += _int(me.get("clears")) == 0
            for k in ("gems", "gold", "kills", "deaths", "clears"):
                t[f"sum_{k}"] += _int(me.get(k))
            t["sum_opp_gems"] += _int(op.get("gems"))
            t["sum_rounds"] += _int(r.get("rounds"))
            t["last"] = max(t["last"], str(r.get("ts") or ""))
    for r in rows:
        if r.get("winner") in FAILED:
            for k in {r.get("a"), r.get("b")}:
                if isinstance(k, str):
                    out[k]["failed"] += 1
    return dict(out)


def standings(rows: list[dict], current: dict[str, str] | None = None, ratings: dict | None = None) -> list[dict]:
    """Ranked entries (rated by Elo, then unrated by key). current = {bot id: key} limits them to
    those keys, and a bot whose current key has no rating gets its last rated version in
    "previous"; None = every key in the log. Entry: key, bot, version, rank, elo, se, games, wins,
    losses, draws, score, faults, exhausted, errors, stalls, failed, previous, and STYLE means."""
    ratings = fit(rows) if ratings is None else ratings
    t_all = tally(rows)
    keys = list(dict.fromkeys(current.values())) if current is not None else sorted(t_all)
    entries = []
    for key in keys:
        t = t_all.get(key) or _blank()
        n, r = t["games"], ratings.get(key)
        e = {"key": key, "bot": bot_of(key), "version": version_of_key(key), "rank": None,
             "elo": r["elo"] if r else None, "se": r["se"] if r else None, "games": n,
             "wins": t["wins"], "losses": t["losses"], "draws": t["draws"],
             "score": (t["wins"] + 0.5 * t["draws"]) / n if n else None,
             "faults": t["faults"], "exhausted": t["exhausted"], "errors": t["errors"], "stalls": t["stalls"],
             "failed": t["failed"], "previous": None}
        for k in STYLE:
            e[k] = t[f"sum_{k}"] / n if n else None
        entries.append(e)
    if current is not None:
        latest: dict[str, tuple] = {}
        for key, t in t_all.items():
            if key in ratings and t["games"] and ((t["last"], key) > latest.get(bot_of(key), ("", ""))):
                latest[bot_of(key)] = (t["last"], key)
        for e in entries:
            prev = latest.get(e["bot"])
            if e["elo"] is None and prev and prev[1] != e["key"]:
                e["previous"] = {"version": version_of_key(prev[1]), "elo": ratings[prev[1]]["elo"],
                                 "games": t_all[prev[1]]["games"]}
    rated = sorted((e for e in entries if e["elo"] is not None), key=lambda e: (-e["elo"], e["key"]))
    for i, e in enumerate(rated, 1):
        e["rank"] = i
    return rated + sorted((e for e in entries if e["elo"] is None), key=lambda e: e["key"])


def _num(v, fmt: str) -> str:
    return "-" if v is None else format(v, fmt)


def _note(e: dict, failed_builds: set) -> str:
    parts = []
    if e["elo"] is None:
        parts.append("build failed" if e["key"] in failed_builds else "no games yet")
        if e.get("previous"):
            p = e["previous"]
            parts.append(f"last rated {p['version']} {p['elo']:.0f} ({p['games']} games)")
    if e.get("failed"):
        parts.append(f"{e['failed']} failed games")
    if e.get("current"):
        parts.append("current")
    return "; ".join(parts)


COLUMNS = ("rank", "bot", "version", "elo", "2se", "games", "score", "W-L-D", "f/x/e/s", "gems", "gold",
           "kills", "deaths", "clears", "note")
LEFT = {"bot", "version", "W-L-D", "f/x/e/s", "note"}


def _cells(e: dict, failed_builds: set) -> dict:
    return {"rank": str(e["rank"] or "-"), "bot": e["bot"], "version": e["version"],
            "elo": _num(e["elo"], ".0f"), "2se": _num(None if e["se"] is None else 2 * e["se"], ".0f"),
            "games": str(e["games"]), "score": "-" if e["score"] is None else f"{100 * e['score']:.1f}%",
            "W-L-D": f"{e['wins']}-{e['losses']}-{e['draws']}" if e["games"] else "-",
            "f/x/e/s": f"{e['faults']}/{e['exhausted']}/{e['errors']}/{e['stalls']}" if e["games"] else "-",
            "gems": _num(e["gems"], ".0f"), "gold": _num(e["gold"], ".0f"), "kills": _num(e["kills"], ".1f"),
            "deaths": _num(e["deaths"], ".1f"), "clears": _num(e["clears"], ".1f"), "note": _note(e, failed_builds)}


def render(entries: list[dict], failed_builds: set = frozenset()) -> list[str]:
    """Aligned text table of standings entries."""
    rows = [_cells(e, failed_builds) for e in entries]
    width = {c: max([len(c)] + [len(r[c]) for r in rows]) for c in COLUMNS}

    def line(cells: dict) -> str:
        return "  ".join(cells[c].ljust(width[c]) if c in LEFT else cells[c].rjust(width[c]) for c in COLUMNS).rstrip()
    return [line({c: c for c in COLUMNS})] + [line(r) for r in rows]


def summary(rows: list[dict], ratings: dict) -> str:
    games = played(rows)
    failed = sum(1 for r in rows if r.get("winner") in FAILED)
    anchor = anchor_key(games)
    where = f"{anchor} = {ANCHOR_ELO:.0f}" if anchor else "mean 1500 (no anchors/starter-cpp games yet)"
    s = f"ladder: {len(games)} games (engine {ENGINE}), {len(ratings)} rated versions, anchor {where}"
    if failed:
        s += f", {failed} failed game rows ignored"
    c = conflicts(rows)
    if c:
        s += f", {c} game keys with conflicting duplicates (nondeterminism?)"
    return s


def failed_builds() -> set:
    return {r.get("key") for r in read_jsonl(BUILDS)}


def write_board(entries: list[dict], rows: list[dict], ratings: dict, path: Path | None = None) -> Path:
    """results/ladder/leaderboard.md from current-version standings entries (atomic replace)."""
    path = Path(path or BOARD)
    fb = failed_builds()
    head = ["Rank", "Bot", "Version", "Elo", "+-2se", "Games", "Score", "W-L-D", "Faults", "Exhausted",
            "Errors", "Stalls", "Gems", "Gold", "Kills", "Deaths", "Clears", "Note"]
    lines = ["# Ladder leaderboard", "",
             f"Generated {_now()} by `arena ladder` from `results/ladder/games.jsonl`. {summary(rows, ratings)}.",
             "",
             "Current versions only (content hash of each bot's build closure in the working tree); older "
             "versions stay in the Bradley-Terry fit as fixed points (`arena ladder table --all-versions`). "
             "Health columns are totals over the version's games (faults, fuel exhaustion, logged error "
             "lines, stalls = games without a camp clear); style columns are means per game. Local strength "
             "only: online strength needs online evidence.", "",
             "| " + " | ".join(head) + " |",
             "|" + "|".join("---" if h in ("Bot", "Version", "W-L-D", "Note") else "---:" for h in head) + "|"]
    for e in entries:
        c = _cells(e, fb)
        hl = [str(e[k]) if e["games"] else "-" for k in ("faults", "exhausted", "errors", "stalls")]
        vals = [c["rank"], f"`{e['bot']}`", e["version"], c["elo"], c["2se"], c["games"], c["score"], c["W-L-D"],
                *hl, c["gems"], c["gold"], c["kills"], c["deaths"], c["clears"], c["note"]]
        lines.append("| " + " | ".join(v.replace("|", "/") for v in vals) + " |")
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(f"{path.name}.tmp-{os.getpid()}")
    tmp.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    for attempt in range(10):
        try:
            os.replace(tmp, path)
            return path
        except PermissionError:  # a reader holds the old file open on Windows
            time.sleep(0.2 * (attempt + 1))
    _unlink(tmp)
    print(f"warning: could not replace {path}", file=sys.stderr)
    return path


# ----------------------------------------------------------------------------- styles, head-to-head, neighbours
def head_to_head(rows: list[dict], xs, ys) -> list[tuple[dict, str]]:
    """Completed games between a key in xs and a key in ys, each with the side of the xs key."""
    xs, ys = set(xs), set(ys)
    out = []
    for r in played(rows):
        if r["a"] in xs and r["b"] in ys:
            out.append((r, "A"))
        elif r["b"] in xs and r["a"] in ys:
            out.append((r, "B"))
    return out


def _record(games: list[tuple[dict, str]]) -> tuple[int, int, int]:
    res = [perspective(r, s)["result"] for r, s in games]
    return res.count("win"), res.count("loss"), res.count("draw")


def styles(rows: list[dict], current: dict[str, str], min_games: int = 4, threshold: float = 0.25) -> list[dict]:
    """Per current version with >= min_games completed games: {key, bot, games, style (raw means of
    STYLE), nearest (key of the closest current version of another bot), dist (RMS z-distance, z
    over all these versions), h2h (w, l, d of key vs nearest), flag}, closest pairs first. flag:
    "possible convergence" (dist < threshold, head-to-head score 35-65%) or "close style, no
    head-to-head" (dist < threshold, never met)."""
    t_all = tally(rows)
    keys = sorted(k for k in set(current.values()) if t_all.get(k, {}).get("games", 0) >= min_games)
    vec = {k: [t_all[k][f"sum_{d}"] / t_all[k]["games"] for d in STYLE] for k in keys}
    dims = range(len(STYLE))
    mean = [statistics.fmean(vec[k][i] for k in keys) if keys else 0.0 for i in dims]
    sd = [statistics.pstdev([vec[k][i] for k in keys]) if len(keys) > 1 else 0.0 for i in dims]
    z = {k: [(vec[k][i] - mean[i]) / sd[i] if sd[i] > 0 else 0.0 for i in dims] for k in keys}
    cur = set(keys)
    h2h: dict[tuple, list] = defaultdict(lambda: [0, 0, 0])
    for r in played(rows):
        if r["a"] in cur and r["b"] in cur:
            i = 0 if r["winner"] == "A" else 1 if r["winner"] == "B" else 2
            h2h[(r["a"], r["b"])][i] += 1
            h2h[(r["b"], r["a"])][(1, 0, 2)[i]] += 1
    out = []
    for k in keys:
        best = None
        for o in keys:
            if bot_of(o) != bot_of(k):
                d = math.sqrt(sum((a - b) ** 2 for a, b in zip(z[k], z[o])) / len(STYLE))
                if best is None or (d, o) < best:
                    best = (d, o)
        item = {"key": k, "bot": bot_of(k), "games": t_all[k]["games"], "style": dict(zip(STYLE, vec[k])),
                "nearest": None, "dist": None, "h2h": (0, 0, 0), "flag": ""}
        if best:
            d, o = best
            w, l, dr = h2h.get((k, o), (0, 0, 0))
            n = w + l + dr
            flag = ""
            if d < threshold:
                if not n:
                    flag = "close style, no head-to-head"
                elif 0.35 <= (w + 0.5 * dr) / n <= 0.65:
                    flag = "possible convergence"
            item.update(nearest=o, dist=d, h2h=(w, l, dr), flag=flag)
        out.append(item)
    return sorted(out, key=lambda s: (s["dist"] is None, s["dist"] or 0.0, s["key"]))


def near(subject, k: int = 4, rows: list[dict] | None = None, current: dict[str, str] | None = None) -> list[Bot]:
    """The k other bots whose current versions are rated closest to the subject (a Bot or bot id):
    its current version's rating, else its last rated version's, else the median (for sweeps'
    `--vs ladder:nearK`)."""
    sid = subject.id if isinstance(subject, Bot) else str(subject)
    rows = load() if rows is None else rows
    ratings = fit(rows)
    current = current_keys() if current is None else current
    rated = {b: ratings[key]["elo"] for b, key in current.items() if key in ratings}
    if sid in rated:
        target = rated[sid]
    else:
        mine = [(t["last"], key) for key, t in tally(rows).items() if bot_of(key) == sid and key in ratings]
        target = ratings[max(mine)[1]]["elo"] if mine else (statistics.median(rated.values()) if rated else 0.0)
    others = sorted((abs(e - target), b) for b, e in rated.items() if b != sid)
    bots = {b.id: b for b in all_bots()}
    return [bots[b] for _, b in others if b in bots][:k]


# ----------------------------------------------------------------------------- running
def _out_dir() -> Path:
    base = RUNS / time.strftime("%Y%m%d-%H%M%S")
    out, n = base, 1
    while True:
        try:
            out.mkdir(parents=True, exist_ok=False)
            return out
        except FileExistsError:
            n += 1
            out = base.with_name(f"{base.name}-{n}")


def _rel(p: Path) -> str:
    p = Path(p).resolve()
    return p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else str(p)


def log_build_failures(ents) -> list:
    """Append failed entrants to results/ladder/builds.jsonl (once per key); returns them."""
    failed = list({e.key: e for e in ents if not e.ok}.values())
    if failed:
        known = {r.get("key") for r in read_jsonl(BUILDS)}
        new = [{"ts": _now(), "key": e.key, "bot": e.bot.id, "error": _tail(e.log)} for e in failed
               if e.key not in known]
        if new:
            append_jsonl(BUILDS, new)
    return failed


def show(entries: list[dict], top: int, more_hint: str = "") -> None:
    shown = entries[:top] if top > 0 else entries
    for line in render(shown, failed_builds()):
        print(line)
    if len(shown) < len(entries):
        print(f"... {len(entries) - len(shown)} more{more_hint}")


def refresh(select_ids: set | None = None, top: int = 15) -> list[dict]:
    """Refit from the log, write the leaderboard and print its top (rows of the selected bots)."""
    rows = load()
    ratings = fit(rows)
    board = standings(rows, current_keys(), ratings)
    path = write_board(board, rows, ratings)
    print(summary(rows, ratings))
    show([e for e in board if select_ids is None or e["bot"] in select_ids], top, f" in {_rel(path)}")
    print(f"leaderboard {_rel(path)}")
    return board


def run_once(spec: str = "all", pairings: int = 0, maps: str = "pool", jobs: int = 0, quiet: bool = False) -> dict:
    """Prepare the selected bots, schedule and play new pairings, log them, refresh the leaderboard.
    Returns {"entrants": [Entrant], "failed": [Entrant], "plan": [Pairing], "rows": [game rows]}."""
    bots = select(spec)
    map_names = map_set(maps)
    if not map_names:
        raise ArenaError("no maps selected")
    ents = prepare_many([(b, None, "") for b in bots])
    failed = log_build_failures(ents)
    good = {e.key: e for e in ents if e.ok}
    for e in failed[:10]:
        print(f"build failed: {e.key} (left out; {_rel(BUILDS)}): {_brief(_tail(e.log, 3))}", flush=True)
    if len(failed) > 10:
        print(f"... {len(failed) - 10} more build failures in {_rel(BUILDS)}", flush=True)
    if len(good) < 2:
        raise ArenaError(f"a ladder run needs at least 2 built entrants; {spec!r} gave {len(good)}")
    n = pairings if pairings > 0 else 2 * len(good)
    with FileLock(RUNS / "schedule.lock"):
        rows = load()
        plan = schedule(list(good), rows, map_names, n, fit(rows), live_claims())
        specs = [GameSpec(good[a], good[b], p.map, {"ladder": True, "engine": ENGINE, "phase": p.phase})
                 for p in plan for a, b in p.games]
        claim = Claim([[g.a.key, g.b.key, g.map] for g in specs]) if specs else None
    phases = Counter(p.phase for p in plan)
    print(f"ladder run: {len(good)} entrants ({spec}), {len(plan)} pairings = {len(specs)} games ({phases['place']} "
          f"placement, {phases['style']} style, {phases['info']} information) on {len(map_names)} maps, at most "
          f"{cap_for(n, len(good))} pairings per entrant"
          + ("" if len(plan) >= n else f"; only {len(plan)} of {n} pairings left unplayed"), flush=True)
    new_rows: list[dict] = []
    if specs:
        try:
            out = _out_dir()
            try:
                # The frozen runner writes canonical local rows before it returns.
                # Retain raw statistics until native diagnostic context is durable.
                play(specs, out, jobs=jobs, keep_replays="all", log=None, quiet=quiet)
            except BaseException as game_error:
                try:
                    finalize_ladder_run(out, LOG)
                except BaseException as publication_error:
                    note = f"health-context finalization also failed; raw outputs retained in {_rel(out)}: {publication_error!r}"
                    game_error.add_note(note)
                    print(note, file=sys.stderr)
                raise
            else:
                new_rows = finalize_ladder_run(out, LOG)
        finally:
            claim.release()
        bad = [r for r in new_rows if r.get("winner") in FAILED]
        notes = []
        for r in new_rows:
            where = f"{r.get('map')} {r.get('a_name')} vs {r.get('b_name')}"
            if r.get("winner") in FAILED:
                notes.append(f"  {where}: {r['winner']}: {_brief(r.get('error'))}")
            elif flagged(r):
                probs = "; ".join(f"{s}: {', '.join(health(r, s))}" for s in "AB" if health(r, s))
                notes.append(f"  {where}: {probs}" + (f"  {r['replay']}" if r.get("replay") else ""))
        print(f"played {len(new_rows)} games ({len(bad)} failed, {len(notes) - len(bad)} with health flags);"
              f" out {_rel(out)}", flush=True)
        for line in notes[:8]:
            print(line)
        if len(notes) > 8:
            print(f"  ... {len(notes) - 8} more in {_rel(out / 'games.jsonl')}")
    refresh({b.id for b in bots}, 15)
    return {"entrants": list(good.values()), "failed": failed, "plan": plan, "rows": new_rows}


# ----------------------------------------------------------------------------- commands
def _cmd_run(args) -> int:
    loop = args.loop or args.max_loops > 0
    n, bad = 0, False
    while True:
        n += 1
        if loop:
            print(f"== ladder loop {n}" + (f" of {args.max_loops}" if args.max_loops else ""), flush=True)
        res = run_once(args.select, args.pairings, args.maps, args.jobs)
        rows = res["rows"]
        failed = [r for r in rows if r.get("winner") in FAILED]
        bad = bad or bool(failed)
        if not loop or (args.max_loops and n >= args.max_loops):
            break
        if rows and len(failed) == len(rows):
            print("every game of this loop failed; stopping", flush=True)
            break
        if not rows:
            print(f"nothing new to play; checking again in {IDLE_WAIT}s (Ctrl-C stops)", flush=True)
            time.sleep(IDLE_WAIT)
    return 1 if bad else 0


def _cmd_table(args) -> int:
    rows = load()
    ratings = fit(rows)
    ids = {b.id for b in select(args.select)}
    cur = current_keys()
    board = standings(rows, cur, ratings)
    path = write_board(board, rows, ratings)
    if args.all_versions:
        now = set(cur.values())
        entries = [dict(e, current=e["key"] in now) for e in standings(rows, None, ratings)
                   if e["bot"] in ids or (_is_all(args.select) and e["bot"] not in cur)]
    else:
        entries = [e for e in board if e["bot"] in ids]
    print(summary(rows, ratings))
    if not entries:
        print("no entries for this selection")
    else:
        show(entries, args.top, " (--top 0 shows all)")
    print(f"leaderboard {_rel(path)}")
    return 0


def _cmd_styles(args) -> int:
    rows = load()
    cur = current_keys()
    ids = {b.id for b in select(args.select)}
    res = styles(rows, cur, args.min_games, args.threshold)
    shown = [s for s in res if s["bot"] in ids]
    print(f"styles: {len(res)} current versions with >= {args.min_games} games (z over all of them); flags: "
          f"distance < {args.threshold} with a head-to-head score of 35-65% or no head-to-head")
    if not shown:
        print("no current versions with enough games in this selection")
        return 0
    head = ("bot", "games", "gems", "gold", "kills", "deaths", "clears", "opp", "rounds", "nearest", "dist", "h2h",
            "flag")
    table = []
    for s in shown[:args.top] if args.top > 0 else shown:
        v = s["style"]
        table.append((s["bot"], str(s["games"]), f"{v['gems']:.0f}", f"{v['gold']:.0f}", f"{v['kills']:.1f}",
                      f"{v['deaths']:.1f}", f"{v['clears']:.1f}", f"{v['opp_gems']:.0f}", f"{v['rounds']:.0f}",
                      bot_of(s["nearest"]) if s["nearest"] else "-",
                      "-" if s["dist"] is None else f"{s['dist']:.2f}", "-".join(map(str, s["h2h"])), s["flag"]))
    width = [max(len(h), *(len(r[i]) for r in table)) for i, h in enumerate(head)]
    left = {0, 9, 11, 12}
    for r in [head] + table:
        print("  ".join(c.ljust(width[i]) if i in left else c.rjust(width[i]) for i, c in enumerate(r)).rstrip())
    if len(table) < len(shown):
        print(f"... {len(shown) - len(table)} more (--top 0 shows all)")
    flags = [s for s in shown if s["flag"]]
    print(f"{sum(1 for s in flags if s['flag'] == 'possible convergence')} possible convergence, "
          f"{sum(1 for s in flags if s['flag'] != 'possible convergence')} close without head-to-head")
    return 0


def _resolve(ident: str, rows: list[dict], all_versions: bool) -> tuple[str, set]:
    """(display name, entrant keys) of a vs argument: an exact key, or a selector matching one bot."""
    if "@" in ident:
        return ident, {ident}
    logged = {k for r in rows for k in (r["a"], r["b"]) if bot_of(k) == ident}
    try:
        b = find_bot(ident)
    except ArenaError:
        if all_versions and logged:
            return ident, logged
        raise
    key = f"{b.id}@{version(b)}"
    if all_versions:
        return b.id, {k for r in rows for k in (r["a"], r["b"]) if bot_of(k) == b.id} | {key}
    return key, {key}


def _cmd_vs(args) -> int:
    rows = load()
    na, xs = _resolve(args.a, rows, args.all_versions)
    nb, ys = _resolve(args.b, rows, args.all_versions)
    games = head_to_head(rows, xs, ys)
    if not games:
        print(f"{na} vs {nb}: no ladder games")
        return 0
    w, l, d = _record(games)
    ps = [perspective(r, s) for r, s in games]
    line = (f"{na} vs {nb}: {w}-{l}-{d} in {len(games)} games (score {100 * (w + 0.5 * d) / len(games):.1f}%), "
            f"gems {statistics.fmean(p['gems'] for p in ps):.0f} vs {statistics.fmean(p['opp_gems'] for p in ps):.0f}"
            " per game")
    ratings = fit(rows)
    if len(xs) == 1 and len(ys) == 1 and next(iter(xs)) in ratings and next(iter(ys)) in ratings:
        ra, rb = ratings[next(iter(xs))], ratings[next(iter(ys))]
        line += (f"; ratings {ra['elo']:.0f} +- {2 * ra['se']:.0f} vs {rb['elo']:.0f} +- {2 * rb['se']:.0f}"
                 f" (expected {100 * expected(ra['elo'], rb['elo']):.1f}%)")
    print(line)
    groups: dict[tuple, list] = defaultdict(list)
    for (r, s), p in zip(games, ps):
        groups[(p["me"], p["opponent"])].append((r, s, p))
    out = []
    if len(groups) == 1:
        by_map: dict[str, dict] = defaultdict(dict)
        for r, s, p in next(iter(groups.values())):
            by_map[r["map"]][s] = f"{p['result'][0].upper()} {p['gems']}-{p['opp_gems']}"
        out.append(f"  {'map':<22} {'as A':<14} as B")
        for m in sorted(by_map):
            out.append(f"  {m:<22} {by_map[m].get('A', '-'):<14} {by_map[m].get('B', '-')}")
    else:
        for (me, op), items in sorted(groups.items()):
            gw, gl, gd = _record([(r, s) for r, s, _ in items])
            out.append(f"  {version_of_key(me)} vs {version_of_key(op)}: {gw}-{gl}-{gd}")
    for s in out[:40]:
        print(s)
    if len(out) > 40:
        print(f"  ... {len(out) - 40} more")
    return 0


def register(sub) -> None:
    p = sub.add_parser("ladder", help="simulated local ladder: every bot version in one shared rated league",
                       description=__doc__.split("\n\n")[0])
    lsub = p.add_subparsers(dest="ladder_command", required=True, metavar="action")
    r = lsub.add_parser("run", help="build current versions, play new informative pairings, refit and rank")
    r.add_argument("select", nargs="?", default="all", help="entrants (bot selectors, default all)")
    r.add_argument("--pairings", type=int, default=0,
                   help="pairings to play, two games each with sides swapped (default 2 per entrant)")
    r.add_argument("--maps", default="pool", help="map set to rotate through (default pool)")
    r.add_argument("--jobs", type=int, default=0, help="machine slots to use (default: all free)")
    r.add_argument("--loop", action="store_true", help="repeat until interrupted, picking up new versions")
    r.add_argument("--max-loops", type=int, default=0, help="with --loop: stop after K loops")
    r.set_defaults(func=_cmd_run)
    t = lsub.add_parser("table", help="ratings, records, health and style; writes results/ladder/leaderboard.md")
    t.add_argument("select", nargs="?", default="all")
    t.add_argument("--all-versions", action="store_true", help="every version in the log, not only current ones")
    t.add_argument("--top", type=int, default=40, help="rows to print (0 = all; the leaderboard file has all)")
    t.set_defaults(func=_cmd_table)
    s = lsub.add_parser("styles", help="style vectors, nearest neighbours and possible convergence")
    s.add_argument("select", nargs="?", default="all")
    s.add_argument("--threshold", type=float, default=STYLE_THRESHOLD,
                   help=f"RMS z-distance below which styles count as close (default {STYLE_THRESHOLD})")
    s.add_argument("--min-games", type=int, default=STYLE_MIN_GAMES,
                   help=f"games a current version needs to be compared (default {STYLE_MIN_GAMES})")
    s.add_argument("--top", type=int, default=40, help="rows to print, closest first (0 = all)")
    s.set_defaults(func=_cmd_styles)
    v = lsub.add_parser("vs", help="head-to-head record of two bots")
    v.add_argument("a")
    v.add_argument("b")
    v.add_argument("--all-versions", action="store_true", help="every version of both bots")
    v.set_defaults(func=_cmd_vs)
